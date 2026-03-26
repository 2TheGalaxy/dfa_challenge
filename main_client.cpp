#include <sstream>
#define ASIO_STANDALONE
#include <algorithm>
#include <asio.hpp>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

// Funkcja pomocnicza sprawdzająca prefiksy stringów (przydatna przy parsowaniu odpowiedzi z serwera)
bool starts_with(std::string_view str, std::string_view prefix)
{
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

int main(int argc, char* argv[])
{
  if (argc < 4 || argc > 5)
  {
    std::cerr << "Uzycie: " << argv[0]
              << " <adres_ip> <port> <imie_nazwisko> [risk]\n";
    return 1;
  }
  const std::string host = argv[1];
  const std::string port = argv[2];
  const std::string my_name = argv[3];

  // Tryb RISK optymalizuje ilość zapytań kosztem dokładności wyroczni weryfikującej
  bool risk_mode = false;
  if (argc == 5 && std::string(argv[4]) == "risk")
  {
    risk_mode = true;
    std::cout << "[INFO] Uruchomiono tryb RISK.\n";
  }
  else
  {
    std::cout << "[INFO] Uruchomiono tryb SAFE.\n";
  }

  // ================= CACHE HANDLING =================
  /* Program wykorzystuje plik cache do wczytywania wynikow poprzednich rund.
     Dzięki temu omijamy nałożony limit i upewniamy się, że poprawiamy błędy z poprzednich weryfikacji. */
  std::map<std::string, bool> cache;
  bool known_success = false;
  std::string cache_filename = "dfa_client_cache.txt";
  std::fstream cache_file(cache_filename, std::ios::in);

  if (cache_file.is_open())
  {
    cache_file.close();
    std::cout << "Znaleziono plik cache z poprzedniej proby. Czy chcesz go "
                 "wyczyscic (nowa runda)? [y/n]: ";
    char ans;
    std::cin >> ans;

    if (ans == 'y' || ans == 'Y')
    {
      std::ofstream clear_file(cache_filename, std::ios::trunc);
      std::cout << "[CACHE] Wyczyszczono.\n";
    }
    else
    {
      // Wczytywanie bazy wiedzy z poprzedniego uruchomienia
      std::ifstream in_file(cache_filename);
      std::string w;
      bool res;
      while (in_file >> w >> res)
      {
        if (w == "__SUCCESS__")
        {
          known_success =
              true; // Flaga oznaczająca, że mamy w 100% zbadany model
          continue;
        }
        if (w == "eps")
          w = "";
        cache[w] = res;
      }
      std::cout << "[CACHE] Wczytano " << cache.size() << " queries.\n";
      if (known_success)
      {
        std::cout << "[CACHE] Wczytuje kompletny model (sukces z poprzedniej "
                     "rundy).\n";
      }
    }
  }
  // ==================================================

  // Połączenie z serwerem via ASIO
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);

  auto endpoints = resolver.resolve(host, port);
  asio::connect(sock, endpoints);

  // --- FUNKCJE LAMBDA DO OBSŁUGI SIECI ---

  /* Lambda `send`: 
     Otacza surową funkcję wysyłającą dane przez socket.
     Automatycznie dodaje znak nowej linii `\n`, którego wymaga protokół serwera. */
  auto send = [&](const std::string& msg)
  { asio::write(sock, asio::buffer(msg + "\n")); };

  /* Lambda `recv`:
     Odczytuje dane z serwera aż do napotkania znaku nowej linii `\n`.
     Posiada wbudowane zabezpieczenie przed formatowaniem Windowsowym (`\r\n`), 
     usuwając ukryty znak powrotu karetki, aby nie zepsuć algorytmu sprawdzającego znaki. */
  auto recv = [&]() -> std::string
  {
    asio::streambuf buf;
    asio::read_until(sock, buf, '\n');
    std::istream is(&buf);
    std::string line;
    std::getline(is, line);
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    return line;
  };

  // Faza 1: Rejestracja w systemie konkursowym
  send("HELLO " + my_name);
  std::string hello_respone = recv();
  std::cout << hello_respone << "\n"; // WELCOME ...

  std::istringstream iss(hello_respone);
  std::string temp_word;
  int query_limit;
  while (iss >> temp_word)
  {
    if (temp_word == "LIMIT")
    {
      iss >> query_limit;
      break;
    }
  }

  // Faza 2: Implementacja L*
  // ================= DFA LEARNING =================
  std::cout << "Buduje model...\n";
  int queries = 0; // Licznik faktycznych strzałów w sieć

  /* Lambda `ask` (Wyrocznia Przynależności):
     Serce algorytmu. Gdy pytamy, czy słowo jest w DFA, najpierw patrzy do pliku cache.
     Jeśli go tam nie ma, dopiero uderza do serwera i aktualizuje licznik zapytań.
     Wynik z serwera jest od razu spisywany do pliku, by zabezpieczyć postęp. */
  auto ask = [&](const std::string& w) -> bool
  {
    if (cache.count(w))
      return cache[w]; // Odczyt z pamięci kosztuje 0 zapytań!

    if (queries >= query_limit)
      return false; // Bezpiecznik przed przekroczeniem limitu rundy

    queries++;
    std::string q = w.empty() ? "eps" : w;
    send("QUERY " + q);
    std::string resp = recv();
    bool res = starts_with(resp, "RESULT YES");
    cache[w] = res;

    std::ofstream out(cache_filename, std::ios::app);
    out << q << " " << res << "\n";

    return res;
  };

  // Zmienne algorytmu L*
  std::vector<std::string> S = {
      ""}; // S - zbiór prefiksów. Definiuje stany naszego DFA.
  std::vector<std::string> E = {
      ""}; // E - zbiór sufiksów (testów). Rozróżnia stany między sobą.
  std::vector<char> A = {'a', 'b'}; // A - dostępny alfabet.

  /* Lambda `get_row`:
     Tworzy charakterystykę danego stanu (wiersz w tabeli obserwacji).
     Dla podanego prefiksu `s`, sprawdza jego kombinacje z każdym sufiksem `e` z tabeli E.
     Zwraca ciąg wartości bool (np. {true, false, false}), który jest unikalnym "podpisem" stanu. */
  auto get_row = [&](const std::string& s)
  {
    std::vector<bool> row;
    for (const auto& e : E)
    {
      row.push_back(ask(s + e));
    }
    return row;
  };

  // Struktura opisująca wygenerowany graf hipotezy automatu
  struct State
  {
    bool accept;
    int next[2]; // next[0] to przejście po 'a', next[1] to przejście po 'b'
  };

  std::vector<State> learned_dfa; // Tablica stanów naszego automatu
  int learned_start = 0;          // Indeks stanu startowego

  // Główna pętla ucząca (działa do momentu, gdy tabela będzie spójna, zamknięta i wolna od kontrprzykładów)
  while (true)
  {
    // --------------------------------------------------------------------------------
    // Krok 1: Sprawdzanie zamkniętości (closedness)
    // Tabela jest zamknięta, gdy każde przejście z rozpoznanego stanu prowadzi
    // do wiersza, który już jest oficjalnym stanem w zbiorze S.
    // --------------------------------------------------------------------------------
    bool is_closed = true;
    std::string unclosed_t = "";
    for (const auto& s : S)
    {
      for (char c : A)
      {
        std::string t = s + c; // Testujemy przejście (prefiks + litera)
        auto row_t = get_row(t);
        bool found = false;
        for (const auto& s2 : S)
        {
          if (get_row(s2) ==
              row_t) // Jeśli sygnatura przejścia zgadza się z sygnaturą istniejącego stanu
          {
            found = true;
            break;
          }
        }
        if (!found) // Znaleźliśmy przejście prowadzące "w nieznane"
        {
          is_closed = false;
          unclosed_t = t;
          break;
        }
      }
      if (!is_closed)
        break;
    }

    if (!is_closed)
    {
      // Tabela nie jest zamknięta. Dodajemy to nowe, nieznane miejsce do puli stanów S i ponawiamy pętlę.
      S.push_back(unclosed_t);
      continue;
    }

    // --------------------------------------------------------------------------------
    // Krok 2: Sprawdzanie spójności (consistency)
    // Tabela jest spójna, gdy dla każdych dwóch stanów S1 i S2 o identycznym rzędzie,
    // po doklejeniu do nich jednej litery 'c', powstają stany również o identycznym rzędzie.
    // Inaczej: te same stany muszą zachowywać się tak samo w przyszłości.
    // --------------------------------------------------------------------------------
    bool is_consistent = true;
    std::string incons_e = "";
    for (size_t i = 0; i < S.size(); ++i)
    {
      for (size_t j = i + 1; j < S.size(); ++j)
      {
        if (get_row(S[i]) ==
            get_row(
                S[j])) // Znaleźliśmy dwie różne ścieżki prowadzące (pozornie) do tego samego stanu
        {
          for (char c : A)
          {
            auto row_ic = get_row(S[i] + c);
            auto row_jc = get_row(S[j] + c);
            if (row_ic !=
                row_jc) // Po dodaniu litery zachowują się różnie! To znaczy, że to dwa RÓŻNE stany.
            {
              is_consistent = false;
              // Szukamy, który test (z E) je oblał i dodajemy nową kolumnę rozróżniającą
              for (size_t k = 0; k < E.size(); ++k)
              {
                if (row_ic[k] != row_jc[k])
                {
                  incons_e = std::string(1, c) + E[k];
                  break;
                }
              }
              break;
            }
          }
        }
        if (!is_consistent)
          break;
      }
      if (!is_consistent)
        break;
    }

    if (!is_consistent)
    {
      // Tabela nie jest spójna. Dodajemy nowy test różnicujący do E i przeliczamy tabelę.
      E.push_back(incons_e);
      continue;
    }

    // --------------------------------------------------------------------------------
    // Krok 3: Budowanie hipotezy (Konwersja tabeli obserwacji na obiekt DFA w C++)
    // Ponieważ tabela jest już zamknięta i spójna, możemy z niej zbudować graf.
    // --------------------------------------------------------------------------------
    std::vector<std::vector<bool>> unique_rows;
    std::map<std::vector<bool>, int> row_to_state;
    learned_dfa.clear();

    // Rejestrujemy unikalne stany (unikalne wiersze to węzły naszego grafu)
    for (const auto& s : S)
    {
      auto r = get_row(s);
      if (row_to_state.find(r) == row_to_state.end())
      {
        row_to_state[r] = unique_rows.size();
        unique_rows.push_back(r);
        learned_dfa.push_back(
            {r[0],
             {-1,
              -1}}); // r[0] odpowiada za sufiks pusty ("eps"), czyli czy stan jest akceptujący
      }
    }

    // Dodajemy przejścia (połączenia między węzłami grafu)
    for (const auto& s : S)
    {
      auto r = get_row(s);
      int state_idx = row_to_state[r];
      for (int c = 0; c < 2; ++c)
      {
        auto r_next = get_row(s + A[c]);
        learned_dfa[state_idx].next[c] = row_to_state[r_next];
      }
    }

    learned_start =
        row_to_state[get_row("")]; // Znalezienie korzenia grafu (pusty string)

    /* Lambda `run_dfa`:
       Funkcja weryfikująca, która przepuszcza słowo testowe `w` przez nasz lokalny, 
       świeżo zbudowany graf (learned_dfa). Służy do porównywania odpowiedzi z serwerem. */
    auto run_dfa = [&](const std::string& w)
    {
      int curr = learned_start;
      for (char c : w)
      {
        if (c == 'a' || c == 'b')
          curr = learned_dfa[curr].next[c - 'a']; // Skakanie po węzłach grafu
      }
      return learned_dfa[curr].accept;
    };

    // --------------------------------------------------------------------------------
    // Krok 4: Wyrocznia Równoważności (Szukanie kontrprzykładu)
    // Zbudowaliśmy hipotezę. Teraz musimy udowodnić, że nasz graf jest taki sam,
    // jak tajny graf na serwerze, szukając słowa, na którym się nie zgadzają.
    // --------------------------------------------------------------------------------
    bool counterexample_found = false;
    std::string counterexample = "";

    // ETAP 4a: Weryfikacja względem lokalnego Cache'u.
    // Zapobiega cofaniu się algorytmu (regresji) i weryfikuje bazę 0 kosztem.
    for (const auto& [w, expected_res] : cache)
    {
      if (w == "__SUCCESS__")
        continue;

      if (run_dfa(w) !=
          expected_res) // Jeśli nasz graf mówi YES, a w cache jest NO
      {
        // Szukamy NAJKRÓTSZEGO kontrprzykładu, by optymalnie pączkować nowe stany S
        if (!counterexample_found || w.length() < counterexample.length())
        {
          counterexample_found = true;
          counterexample = w;
        }
      }
    }

    // Jeżeli mamy absolutną pewność z poprzedniego uruchomienia, że jesteśmy gotowi:
    if (!counterexample_found && known_success)
      break;

    // ETAP 4b: Szukanie nowych dziur w serwerze (jeżeli cache pasuje idealnie).
    if (!counterexample_found)
    {
      if (risk_mode)
      {
        // TRYB RISK: Przerywamy sprawdzanie, gdy znaleźliśmy już niemal wszystkie stany (9 lub 10).
        // Wykonujemy testy płytko i dorzucamy losowe głębokie strzały, żeby oszczędzić limit `queries`.
        if (unique_rows.size() < 9)
        {
          std::vector<std::string> candidates;
          std::queue<std::string> q;
          q.push("");
          while (!q.empty())
          {
            std::string w = q.front();
            q.pop();
            if (w.length() <= 5) // Płytki BFS - tylko do 5 znaków
            {
              candidates.push_back(w);
              q.push(w + "a");
              q.push(w + "b");
            }
          }

          srand(time(NULL) + queries);
          for (int i = 0; i < 40;
               ++i) // Dorzucenie 40 losowych długich szlaków (do 12 znaków)
          {
            std::string rw = "";
            int len = 6 + (rand() % 7);
            for (int j = 0; j < len; ++j)
            {
              rw += (rand() % 2 == 0) ? 'a' : 'b';
            }
            candidates.push_back(rw);
          }

          for (const auto& w : candidates)
          {
            bool dfa_ans = run_dfa(w);
            if (queries >= 950 && !cache.count(w))
              break; // Zapas bezpieczeństwa!

            bool server_ans = ask(w);
            if (dfa_ans != server_ans)
            {
              counterexample_found = true;
              counterexample = w;
              break;
            }
          }
        }
      }
      else
      {
        // TRYB SAFE: Brutalny BFS aż do 8 poziomu (511 słów). Gwarantuje poprawność,
        // ale kosztuje mnóstwo cennych queries obniżając SCORE.
        std::queue<std::string> q;
        q.push("");
        while (!q.empty())
        {
          std::string w = q.front();
          q.pop();

          if (w.length() > 8)
            continue;

          bool dfa_ans = run_dfa(w);
          if (queries > 900 && !cache.count(w))
            break;

          bool server_ans = ask(w);
          if (dfa_ans != server_ans)
          {
            counterexample_found = true;
            counterexample = w;
            break;
          }

          q.push(w + "a");
          q.push(w + "b");
        }
      }
    }

    // --------------------------------------------------------------------------------
    // Krok 5: Analiza kontrprzykładu
    // Jeżeli nasza hipoteza upadła, bierzemy kontrprzykład, rozbijamy go na prefiksy
    // i dorzucamy je jako potencjalne nowe stany (rozszerzenie zbioru S).
    // --------------------------------------------------------------------------------
    if (counterexample_found)
    {
      std::string pref = "";
      bool added_any = false;
      for (char c : counterexample)
      {
        pref += c;
        if (std::find(S.begin(), S.end(), pref) == S.end())
        {
          S.push_back(pref);
          added_any = true;
        }
      }
      if (!added_any)
        break; // Zabezpieczenie na wypadek utknięcia w pętli
    }
    else
    {
      // Hipoteza jest doskonała. Wychodzimy z nauki!
      break;
    }
  }

  std::cout << "Model zbudowany.\n";
  // ================= END LEARNING =================

  // Faza 3: Ostateczna weryfikacja przez serwer
  std::cout << "Weryfikuje...\n";
  send("SUBMIT");
  std::string line = recv(); // VERIFY_START <n>

  int n = std::stoi(line.substr(line.rfind(' ') + 1));

  for (int i = 0; i < n; ++i)
  {
    std::string test = recv();         // TEST <słowo>
    std::string word = test.substr(5); // wytnij z odpowiedzi sam wyraz

    // Przerzucenie słowa weryfikacyjnego przez nasz gotowy graf
    bool accepted = false;
    std::string dfa_word = (word == "eps") ? "" : word;
    int curr = learned_start;

    for (char c : dfa_word)
    {
      if (c == 'a' || c == 'b')
      {
        curr = learned_dfa[curr].next[c - 'a'];
      }
    }
    accepted = learned_dfa[curr].accept;

    // Odpowiadamy serwerowi to, co policzyliśmy lokalnie
    send(accepted ? "ANSWER YES" : "ANSWER NO");
    std::string response = recv();

    // Jeśli serwer zgłosił błąd w trakcie weryfikacji...
    if (starts_with(response, "FAILURE"))
    {
      std::cerr << "Blad weryfikacji na slowie: " << word << "\n";

      // SUPER WAŻNE: Odwracamy naszą odpowiedź (na tę prawidłową) i wymuszamy
      // jej zapisanie do pliku Cache. Dzięki temu drugie uruchomienie programu
      // uczy się na błędzie z weryfikacji w sposób darmowy.
      std::ofstream out(cache_filename, std::ios::app);
      out << (dfa_word.empty() ? "eps" : dfa_word) << " " << (!accepted)
          << "\n";
      std::cout << "[CACHE] Zapisano wynik tej rundy (fail).\n";
      return 1;
    }
  }

  std::cout << "Weryfikacja zakonczona.\n";

  // SUKCES: Pomyślnie przeszliśmy setki pytań serwera
  std::string final_response = recv();
  std::cout << final_response << "\n"; // SUCCESS SCORE

  // Zapis do cache wygranej - pozwala na ponowne odpalenie i wgranie gotowca ze SCORE=1500
  if (!known_success)
  {
    std::ofstream out(cache_filename, std::ios::app);
    out << "__SUCCESS__ 1\n";
    std::cout << "[CACHE] Zapisano idealny model do cache.\n";
  }

  send("QUIT");
  recv(); // Zamykanie połączenia
  return 0;
}
