/*
 * dfa_server.cpp  â€“  Serwer konkursu "DFA Challenge"
 *
 * Kompilacja (Linux / GCC):
 *   g++ -std=c++17 -O2 -DASIO_STANDALONE \
 *       -I<Å›cieÅ¼ka_do_asio/include> -lpthread \
 *       dfa_server.cpp -o dfa_server
 *
 * Uruchomienie:
 *   ./dfa_server [port=9000] [seed=losowe]
 *
 * Windows (MSVC / vcpkg):
 *   cl /std:c++17 /O2 /DASIO_STANDALONE /EHsc dfa_server.cpp
 *      /I<vcpkg_root>\installed\x64-windows\include
 *      /link ws2_32.lib
 */

#include <asio.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// DFA
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

struct DFA
{
  static constexpr int N = 10;
  int delta[N][2]{}; // delta[stan][0=a, 1=b] = stan docelowy
  bool accept[N]{};
  int start = 0;

  bool run(const std::string& w) const
  {
    int q = start;
    for (char c : w)
      q = delta[q][c == 'a' ? 0 : 1];
    return accept[q];
  }
};

DFA generate_random_dfa(std::mt19937& rng)
{
  DFA d;
  std::uniform_int_distribution<int> st(0, DFA::N - 1);
  for (int i = 0; i < DFA::N; ++i)
    for (int s = 0; s < 2; ++s)
      d.delta[i][s] = st(rng);

  // DokÅ‚adnie 5 stanÃ³w akceptujÄ…cych
  std::vector<int> idx(DFA::N);
  std::iota(idx.begin(), idx.end(), 0);
  std::shuffle(idx.begin(), idx.end(), rng);
  for (int i = 0; i < 5; ++i)
    d.accept[idx[i]] = true;

  return d;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Generowanie zestawu testowego metodÄ… W (Chow 1978)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

// Pokrycie stanÃ³w P: dla kaÅ¼dego stanu q najkrÃ³tsze sÅ‚owo prowadzÄ…ce z q0 do q
std::vector<std::string> compute_state_cover(const DFA& d)
{
  const int n = DFA::N;
  std::vector<std::string> P(n, "\x01"); // sentinel = nieosiÄ…galny
  std::vector<bool> vis(n, false);
  std::queue<std::pair<int, std::string>> bfs;
  bfs.push({d.start, ""});
  vis[d.start] = true;
  P[d.start] = "";
  while (!bfs.empty())
  {
    auto [q, w] = bfs.front();
    bfs.pop();
    for (int a = 0; a < 2; ++a)
    {
      int nq = d.delta[q][a];
      if (!vis[nq])
      {
        vis[nq] = true;
        P[nq] = w + (char)("ab"[a]);
        bfs.push({nq, P[nq]});
      }
    }
  }
  // Stany nieosiÄ…galne dostajÄ… pusty ciÄ…g (delta robocza)
  for (auto& p : P)
    if (p == "\x01")
      p = "";
  return P;
}

// ZbiÃ³r charakteryzujÄ…cy W: sÅ‚owa rozrÃ³Å¼niajÄ…ce wszystkie pary stanÃ³w
// (algorytm table-filling Moora)
std::vector<std::string> compute_W(const DFA& d)
{
  const int n = DFA::N;
  bool dist[n][n];
  std::string wit[n][n];
  memset(dist, 0, sizeof(dist));

  // Inicjalizacja: pary rÃ³Å¼niÄ…ce siÄ™ akceptacjÄ…
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j)
      if (d.accept[i] != d.accept[j])
      {
        dist[i][j] = dist[j][i] = true;
        wit[i][j] = wit[j][i] = "";
      }

  bool changed = true;
  while (changed)
  {
    changed = false;
    for (int i = 0; i < n; ++i)
      for (int j = i + 1; j < n; ++j)
      {
        if (dist[i][j])
          continue;
        for (int a = 0; a < 2 && !dist[i][j]; ++a)
        {
          int di = d.delta[i][a], dj = d.delta[j][a];
          if (di != dj)
          {
            int lo = std::min(di, dj), hi = std::max(di, dj);
            if (dist[lo][hi])
            {
              dist[i][j] = dist[j][i] = true;
              wit[i][j] = wit[j][i] = std::string(1, "ab"[a]) + wit[lo][hi];
              changed = true;
            }
          }
        }
      }
  }

  std::set<std::string> W;
  W.insert(""); // sÅ‚owo puste zawsze w W
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j)
      if (dist[i][j])
        W.insert(wit[i][j]);

  return {W.begin(), W.end()};
}

std::vector<std::string> generate_test_suite(const DFA& d, std::mt19937& rng)
{
  auto P = compute_state_cover(d);
  auto W = compute_W(d);

  std::set<std::string> suite;

  // P (pokrycie stanÃ³w)
  for (auto& p : P)
    suite.insert(p);

  // P Â· {a,b} Â· W  (rdzeÅ„ metody W)
  for (auto& p : P)
    for (char a : {'a', 'b'})
      for (auto& w : W)
        suite.insert(p + a + w);

  // Losowe sÅ‚owa dla dodatkowego bezpieczeÅ„stwa
  std::uniform_int_distribution<int> len_d(1, 30);
  std::uniform_int_distribution<int> sym_d(0, 1);
  for (int i = 0; i < 300; ++i)
  {
    std::string word;
    for (int j = 0, L = len_d(rng); j < L; ++j)
      word += (char)("ab"[sym_d(rng)]);
    suite.insert(word);
  }

  std::vector<std::string> v(suite.begin(), suite.end());
  std::shuffle(v.begin(), v.end(), rng);
  return v;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Sesja klienta
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static constexpr int QUERY_LIMIT = 1000;

std::mutex g_names_mutex;
std::set<std::string> g_active_names;

class Session
{
public:
  Session(asio::ip::tcp::socket sock, const DFA& dfa, std::mt19937 rng_copy)
      : socket_(std::move(sock)), dfa_(dfa), rng_(rng_copy)
  {
  }

  void run()
  {
    try
    {
      phase_register();
      phase_query();
      phase_verify();
    }
    catch (const asio::system_error& e)
    {
      if (e.code() != asio::error::eof &&
          e.code() != asio::error::connection_reset)
        std::cerr << tag() << "network error: " << e.what() << "\n";
    }
    catch (const std::exception& e)
    {
      std::cerr << tag() << "exception: " << e.what() << "\n";
    }
    release_name();
    std::cout << tag() << "session closed  queries=" << queries_used_ << "\n";
  }

private:
  // â”€â”€ I/O helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  std::string recv_line()
  {
    asio::read_until(socket_, buf_, '\n');
    std::istream is(&buf_);
    std::string line;
    std::getline(is, line);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    return line;
  }

  void send(const std::string& msg)
  {
    asio::write(socket_, asio::buffer(msg + "\n"));
  }

  std::string tag() const
  {
    return "[" + sid_ + "|" + name_ + "] ";
  }

  // â”€â”€ Faza 1: Rejestracja â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  void phase_register()
  {
    auto hello = recv_line();
    if (hello.size() < 7 || hello.substr(0, 6) != "HELLO ")
    {
      send("ERROR INVALID_HANDSHAKE expected: HELLO <name>");
      throw std::runtime_error("bad handshake");
    }
    name_ = hello.substr(6);
    for (char c : name_)
      if (!isalnum(c) && c != '_')
      {
        send("ERROR INVALID_NAME only [a-zA-Z0-9_] allowed");
        throw std::runtime_error("invalid name");
      }
    if (name_.empty() || name_.size() > 32)
    {
      send("ERROR INVALID_NAME length must be 1-32");
      throw std::runtime_error("invalid name length");
    }
    {
      std::lock_guard<std::mutex> lk(g_names_mutex);
      if (g_active_names.count(name_))
      {
        send("ERROR NAME_TAKEN");
        throw std::runtime_error("name taken");
      }
      g_active_names.insert(name_);
    }
    sid_ = make_sid();
    send("WELCOME " + sid_ + " LIMIT " + std::to_string(QUERY_LIMIT));
    std::cout << tag() << "connected from "
              << socket_.remote_endpoint().address().to_string() << "\n";
  }

  // â”€â”€ Faza 2: Zapytania o przynaleÅ¼noÅ›Ä‡ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  void phase_query()
  {
    while (true)
    {
      auto line = recv_line();
      if (line == "SUBMIT")
      {
        return;
      }
      if (line == "QUIT")
      {
        send("BYE");
        throw std::runtime_error("quit");
      }
      if (line == "STATUS")
      {
        send("STATUS QUERIES_USED " + std::to_string(queries_used_) +
             " QUERIES_LEFT " + std::to_string(QUERY_LIMIT - queries_used_));
        continue;
      }
      if (line.size() >= 7 && line.substr(0, 6) == "QUERY ")
      {
        if (queries_used_ >= QUERY_LIMIT)
        {
          send("ERROR LIMIT_EXCEEDED");
          continue;
        }
        std::string word = line.substr(6);
        if (word == "eps")
          word = "";
        else if (!is_valid_word(word))
        {
          send("ERROR INVALID_WORD");
          continue;
        }
        ++queries_used_;
        send(dfa_.run(word) ? "RESULT YES" : "RESULT NO");
        continue;
      }
      send("ERROR UNKNOWN_COMMAND");
    }
  }

  // â”€â”€ Faza 3: Weryfikacja â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  void phase_verify()
  {
    auto suite = generate_test_suite(dfa_, rng_);
    send("VERIFY_START " + std::to_string(suite.size()));
    std::cout << tag() << "verification started  tests=" << suite.size()
              << "\n";

    for (auto& word : suite)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::string display = word.empty() ? "eps" : word;
      send("TEST " + display);

      auto answer = recv_line();
      if (answer != "ANSWER YES" && answer != "ANSWER NO")
      {
        send("ERROR EXPECTED_ANSWER_YES_OR_NO");
        send("FAILURE WORD " + display);
        return;
      }
      bool client_yes = (answer == "ANSWER YES");
      bool correct_yes = dfa_.run(word);

      if (client_yes != correct_yes)
      {
        send("FAILURE WORD " + display);
        std::cout << tag() << "FAILURE  first_error=" << display << "\n";
        return;
      }
      else
      {
        send("ACK");
      }
    }

    int bonus_q = std::max(0, 500 - queries_used_ / 2);
    int score = 1000 + bonus_q;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    send("SUCCESS SCORE " + std::to_string(score));
    std::cout << tag() << "SUCCESS  score=" << score
              << "  queries=" << queries_used_ << "\n";
    auto squit = recv_line();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    send("BYE");
  }

  // â”€â”€ Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  static bool is_valid_word(const std::string& w)
  {
    if (w.size() > 512)
      return false;
    for (char c : w)
      if (c != 'a' && c != 'b')
        return false;
    return true;
  }

  std::string make_sid()
  {
    std::uniform_int_distribution<uint32_t> d;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(4) << (d(rng_) & 0xFFFF)
        << std::setw(4) << (d(rng_) & 0xFFFF);
    return oss.str();
  }

  void release_name()
  {
    if (!name_.empty())
    {
      std::lock_guard<std::mutex> lk(g_names_mutex);
      g_active_names.erase(name_);
    }
  }

  asio::ip::tcp::socket socket_;
  asio::streambuf buf_;
  const DFA& dfa_;
  std::mt19937 rng_;
  std::string name_;
  std::string sid_;
  int queries_used_ = 0;
};

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// main
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

int main(int argc, char* argv[])
{
  uint16_t port = 9000;
  uint64_t seed = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());

  if (argc >= 2)
    port = static_cast<uint16_t>(std::stoul(argv[1]));
  if (argc >= 3)
    seed = std::stoull(argv[2]);

  std::mt19937 rng(seed);
  DFA hidden = generate_random_dfa(rng);

  // Wydrukuj automat (tylko dla organizatora!)
  std::cout << "=== Ukryty DFA  (seed=" << seed << ") ===\n";
  std::cout << "Stany akceptujÄ…ce: ";
  for (int i = 0; i < DFA::N; ++i)
    if (hidden.accept[i])
      std::cout << i << " ";
  std::cout << "\nPrzejÅ›cia:\n";
  for (int i = 0; i < DFA::N; ++i)
    std::cout << "  delta(" << i << ",a)=" << hidden.delta[i][0] << "  delta("
              << i << ",b)=" << hidden.delta[i][1] << "\n";
  std::cout << "=========================================\n";

  asio::io_context io;
  asio::ip::tcp::acceptor acceptor(
      io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port));

  std::cout << "Serwer nasÅ‚uchuje na porcie " << port << "\n";

  while (true)
  {
    asio::ip::tcp::socket sock(io);
    acceptor.accept(sock);

    // KaÅ¼da sesja dostaje wÅ‚asnÄ… kopiÄ™ rng (deterministycznÄ…, ale innÄ…)
    std::mt19937 session_rng(rng());

    std::thread(
        [s = std::move(sock), &hidden, srng = session_rng]() mutable
        {
          Session session(std::move(s), hidden, srng);
          session.run();
        })
        .detach();
  }
  return 0;
}
