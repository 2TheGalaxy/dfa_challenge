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

struct Config
{
  int max_queries = 1000;
};

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

  Config conf;

  // ================= CACHE HANDLING =================
  /*Program wykorzystuje plik cache do wczytywania wynikow poprzednich rund*/
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
      std::ifstream in_file(cache_filename);
      std::string w;
      bool res;
      while (in_file >> w >> res)
      {
        if (w == "__SUCCESS__")
        {
          known_success = true;
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

  // Połączenie z serverem
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  asio::ip::tcp::socket sock(io);

  auto endpoints = resolver.resolve(host, port);
  asio::connect(sock, endpoints);

  // Pomocnicze funkcje I/O
  auto send = [&](const std::string& msg)
  { asio::write(sock, asio::buffer(msg + "\n")); };

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

  // Faza 1: Rejestracja
  send("HELLO " + my_name);
  std::cout << recv() << "\n"; // WELCOME ...

  // Faza 2: Implementacja
  // ================= DFA LEARNING =================
  std::cout << "Buduje model...\n";
  int queries = 0; // zliczamy queries, by sprawdzic czy nie przekroczy limitu

  /*Funkcja, ktora pyta serwer o przynaleznosc slowa do DFA.
  Wpisuje ona rowniez wynik zapytania do pliku cache.
  Ograniczona maxymalnym queries wg. zasad gry.*/
  auto ask = [&](const std::string& w) -> bool
  {
    // jeżeli zapytanie jest już w cache to nie zuzywamy zapytan
    if (cache.count(w))
      return cache[w];

    if (queries >= conf.max_queries)
      return false;

    queries++;
    std::string q = w.empty() ? "eps" : w;
    send("QUERY " + q);
    std::string resp = recv();
    bool res = starts_with(resp, "RESULT YES");
    cache[w] = res;

    std::ofstream out(cache_filename, std::ios::app); // zapisujemy do pliku
    out << q << " " << res << "\n";

    return res;
  };

  std::vector<std::string> S = {""}; // prefixy (stany)
  std::vector<std::string> E = {""}; // sufixy (testy)
  std::vector<char> A = {'a', 'b'};  // dostepne znaki alfabetu

  auto get_row = [&](const std::string& s)
  {
    std::vector<bool> row;
    for (const auto& e : E)
    {
      row.push_back(ask(s + e));
    }
    return row;
  };

  struct State
  {
    bool accept;
    int next[2];
  };

  std::vector<State> learned_dfa;
  int learned_start = 0;

  while (true)
  {
    // Krok 1: closedness
    bool is_closed = true;
    std::string unclosed_t = "";
    for (const auto& s : S)
    {
      for (char c : A)
      {
        std::string t = s + c;
        auto row_t = get_row(t);
        bool found = false;
        for (const auto& s2 : S)
        {
          if (get_row(s2) == row_t)
          {
            found = true;
            break;
          }
        }
        if (!found)
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
      S.push_back(unclosed_t);
      continue;
    }

    // Krok 2: Spójność (consistency)
    bool is_consistent = true;
    std::string incons_e = "";
    for (size_t i = 0; i < S.size(); ++i)
    {
      for (size_t j = i + 1; j < S.size(); ++j)
      {
        if (get_row(S[i]) == get_row(S[j]))
        {
          for (char c : A)
          {
            auto row_ic = get_row(S[i] + c);
            auto row_jc = get_row(S[j] + c);
            if (row_ic != row_jc)
            {
              is_consistent = false;
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
      E.push_back(incons_e);
      continue;
    }

    // Krok 3: Budowanie hipotezy DFA
    std::vector<std::vector<bool>> unique_rows;
    std::map<std::vector<bool>, int> row_to_state;
    learned_dfa.clear();

    for (const auto& s : S)
    {
      auto r = get_row(s);
      if (row_to_state.find(r) == row_to_state.end())
      {
        row_to_state[r] = unique_rows.size();
        unique_rows.push_back(r);
        learned_dfa.push_back({r[0], {-1, -1}});
      }
    }

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

    learned_start = row_to_state[get_row("")];
    auto run_dfa = [&](const std::string& w)
    {
      int curr = learned_start;
      for (char c : w)
      {
        if (c == 'a' || c == 'b')
          curr = learned_dfa[curr].next[c - 'a'];
      }
      return learned_dfa[curr].accept;
    };

    // Krok 4: Wyrocznia
    bool counterexample_found = false;
    std::string counterexample = "";

    // NAJPIERW sprawdzamy naszą hipotezę na tle WSZYSTKICH danych z pliku cache.
    // Wybieramy NAJKRÓTSZY kontrprzykład, by optymalnie budować graf.
    for (const auto& [w, expected_res] : cache)
    {
      if (w == "__SUCCESS__")
        continue;
      if (run_dfa(w) != expected_res)
      {
        if (!counterexample_found || w.length() < counterexample.length())
        {
          counterexample_found = true;
          counterexample = w;
        }
      }
    }

    // Jeżeli nasza hipoteza idealnie pasuje do cache'u, a wiemy że ten cache
    // doprowadził do sukcesu w poprzednim uruchomieniu -> możemy przerwać uczenie!
    if (!counterexample_found && known_success)
    {
      break;
    }

    // Jeśli w cache nie było błędu, odpytujemy serwer (zależnie od trybu)
    if (!counterexample_found)
    {
      if (risk_mode)
      {
        if (unique_rows.size() < 9)
        {
          std::vector<std::string> candidates;
          std::queue<std::string> q;
          q.push("");
          while (!q.empty())
          {
            std::string w = q.front();
            q.pop();
            if (w.length() <= 5)
            {
              candidates.push_back(w);
              q.push(w + "a");
              q.push(w + "b");
            }
          }

          srand(time(NULL) + queries);
          for (int i = 0; i < 40; ++i)
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
              break;

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

    // Dodanie znalezionego kontrprzykładu do tabeli
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
        break; // Bezpiecznik: jeśli żaden nowy prefiks nie został dodany
    }
    else
    {
      break;
    }
  }

  std::cout << "Model zbudowany.\n";
  // ================= END LEARNING =================

  // Faza 3: Weryfikacja
  std::cout << "Weryfikuje...\n";
  send("SUBMIT");
  std::string line = recv(); // VERIFY_START <n>

  // n = ilość testowych zapytań od serwera
  int n = std::stoi(line.substr(line.rfind(' ') + 1));

  for (int i = 0; i < n; ++i)
  {
    std::string test = recv();         // TEST <słowo>
    std::string word = test.substr(5); // wytnij "TEST "

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

    send(accepted ? "ANSWER YES" : "ANSWER NO");
    std::string response = recv();

    if (starts_with(response, "FAILURE"))
    {
      std::cerr << "Blad weryfikacji na slowie: " << word << "\n";
      std::ofstream out(cache_filename, std::ios::app);
      out << (dfa_word.empty() ? "eps" : dfa_word) << " " << (!accepted)
          << "\n";
      std::cout << "[CACHE] Zapisano wynik tej rundy (fail).\n";
      return 1;
    }
  }

  std::cout << "Weryfikacja zakonczona.\n";

  std::string final_response = recv();
  std::cout << final_response << "\n"; // SUCCESS SCORE

  // ZAPIS SUKCESU DO CACHE
  if (!known_success)
  {
    std::ofstream out(cache_filename, std::ios::app);
    out << "__SUCCESS__ 1\n";
    std::cout << "[CACHE] Zapisano idealny model do cache.\n";
  }

  send("QUIT");
  recv(); // BYE
  return 0;
}
