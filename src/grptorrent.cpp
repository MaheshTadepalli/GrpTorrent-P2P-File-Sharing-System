#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSESOCK close
#endif

using namespace std;

static const int BLOCK_SIZE = 16 * 1024;
static const int PIPELINE = 6;

static uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

struct SHA1 {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
  uint64_t bytes = 0;
  vector<unsigned char> buf;
  void process(const unsigned char *chunk) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(chunk[i * 4]) << 24) | (uint32_t(chunk[i * 4 + 1]) << 16) |
             (uint32_t(chunk[i * 4 + 2]) << 8) | uint32_t(chunk[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t temp = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = temp;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
  }
  void update(const unsigned char *data, size_t len) {
    bytes += len;
    size_t i = 0;
    if (!buf.empty()) {
      while (i < len && buf.size() < 64) buf.push_back(data[i++]);
      if (buf.size() == 64) { process(buf.data()); buf.clear(); }
    }
    while (i + 64 <= len) { process(data + i); i += 64; }
    while (i < len) buf.push_back(data[i++]);
  }
  string final_hex() {
    uint64_t bits = bytes * 8;
    buf.push_back(0x80);
    while (buf.size() % 64 != 56) buf.push_back(0);
    for (int i = 7; i >= 0; --i) buf.push_back((bits >> (i * 8)) & 0xff);
    for (size_t i = 0; i < buf.size(); i += 64) process(buf.data() + i);
    stringstream ss; ss << hex << setfill('0');
    uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) ss << setw(8) << hs[i];
    return ss.str();
  }
};

static string sha1_hex(const vector<unsigned char> &d) {
  SHA1 s; if (!d.empty()) s.update(d.data(), d.size()); return s.final_hex();
}
static string sha1_text(const string &d) {
  SHA1 s; s.update((const unsigned char*)d.data(), d.size()); return s.final_hex();
}
static string hex_of(const vector<unsigned char> &d) {
  static const char *h = "0123456789abcdef"; string out; out.reserve(d.size() * 2);
  for (unsigned char c : d) { out.push_back(h[c >> 4]); out.push_back(h[c & 15]); }
  return out;
}
static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}
static vector<unsigned char> unhex(const string &s) {
  vector<unsigned char> out; out.reserve(s.size() / 2);
  for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back((hexval(s[i]) << 4) | hexval(s[i + 1]));
  return out;
}

struct NetInit {
  NetInit() {
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
#endif
  }
  ~NetInit() {
#ifdef _WIN32
    WSACleanup();
#endif
  }
};

static bool send_all(socket_t s, const string &data) {
  const char *p = data.data(); size_t left = data.size();
  while (left) {
    int n = send(s, p, (int)left, 0);
    if (n <= 0) return false;
    p += n; left -= n;
  }
  return true;
}
static bool read_line(socket_t s, string &line) {
  line.clear(); char c;
  while (true) {
    int n = recv(s, &c, 1, 0);
    if (n <= 0) return false;
    if (c == '\n') return true;
    if (c != '\r') line.push_back(c);
    if (line.size() > 8 * 1024 * 1024) return false;
  }
}
static vector<string> split(const string &s, char d) {
  vector<string> r; string cur; stringstream ss(s);
  while (getline(ss, cur, d)) r.push_back(cur);
  return r;
}
static string trim(string s) {
  while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  return s;
}
static string urlenc(const string &s) {
  stringstream o; o << hex << uppercase;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.') o << c;
    else { o << '%' << setw(2) << setfill('0') << int(c); }
  }
  return o.str();
}
static socket_t connect_tcp(const string &host, int port, int timeout_ms = 3000) {
  (void)timeout_ms;
  addrinfo hints; memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
  addrinfo *res = nullptr;
  string p = to_string(port);
  if (getaddrinfo(host.c_str(), p.c_str(), &hints, &res) != 0) return INVALID_SOCKET;
  socket_t sock = INVALID_SOCKET;
  for (addrinfo *a = res; a; a = a->ai_next) {
    sock = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (sock == INVALID_SOCKET) continue;
    if (connect(sock, a->ai_addr, (int)a->ai_addrlen) == 0) break;
    CLOSESOCK(sock); sock = INVALID_SOCKET;
  }
  freeaddrinfo(res);
  return sock;
}

struct TorrentMeta {
  string announce, name, info_hash;
  long long length = 0;
  int piece_length = 0;
  vector<string> pieces;
  int piece_count() const { return (int)pieces.size(); }
};

static map<string,string> parse_kv_file(const string &path) {
  ifstream in(path.c_str(), ios::binary);
  if (!in) throw runtime_error("cannot open metadata: " + path);
  map<string,string> kv; string line;
  while (getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    size_t p = line.find('=');
    if (p != string::npos) kv[line.substr(0, p)] = line.substr(p + 1);
  }
  return kv;
}
static TorrentMeta load_meta(const string &path) {
  auto kv = parse_kv_file(path); TorrentMeta m;
  m.announce = kv["announce"]; m.name = kv["name"];
  m.length = atoll(kv["length"].c_str()); m.piece_length = atoi(kv["piece_length"].c_str());
  string ph = kv["pieces"];
  for (size_t i = 0; i + 39 < ph.size(); i += 40) m.pieces.push_back(ph.substr(i, 40));
  m.info_hash = kv["info_hash"];
  if (m.info_hash.empty()) m.info_hash = sha1_text(m.name + "|" + to_string(m.length) + "|" + to_string(m.piece_length) + "|" + ph);
  if (m.announce.empty() || m.pieces.empty() || m.piece_length <= 0) throw runtime_error("bad metadata file");
  return m;
}
static void create_torrent(const string &input, const string &tracker, const string &out, int piece_len) {
  ifstream in(input.c_str(), ios::binary);
  if (!in) throw runtime_error("cannot read input file");
  vector<string> hashes; vector<unsigned char> buf(piece_len);
  long long total = 0;
  while (in) {
    in.read((char*)buf.data(), piece_len);
    streamsize n = in.gcount();
    if (n <= 0) break;
    total += n;
    vector<unsigned char> piece(buf.begin(), buf.begin() + n);
    hashes.push_back(sha1_hex(piece));
  }
  string name = input;
  size_t slash = name.find_last_of("/\\"); if (slash != string::npos) name = name.substr(slash + 1);
  string all; for (auto &h : hashes) all += h;
  string ih = sha1_text(name + "|" + to_string(total) + "|" + to_string(piece_len) + "|" + all);
  ofstream o(out.c_str(), ios::binary);
  o << "# GrpTorrent compact torrent metadata\n";
  o << "announce=" << tracker << "\nname=" << name << "\nlength=" << total << "\npiece_length=" << piece_len << "\npieces=" << all << "\ninfo_hash=" << ih << "\n";
}

struct State {
  TorrentMeta meta;
  string data_path, state_path;
  vector<bool> have;
  mutex mu;
  State(const TorrentMeta &m, const string &d, const string &s): meta(m), data_path(d), state_path(s), have(m.piece_count(), false) {}
  int piece_size(int idx) {
    long long off = 1LL * idx * meta.piece_length;
    return (int)min<long long>(meta.piece_length, meta.length - off);
  }
  vector<unsigned char> read_piece(int idx) {
    int sz = piece_size(idx); vector<unsigned char> b(sz);
    ifstream in(data_path.c_str(), ios::binary); in.seekg(1LL * idx * meta.piece_length);
    in.read((char*)b.data(), sz);
    if (in.gcount() != sz) b.resize(max<streamsize>(0, in.gcount()));
    return b;
  }
  void write_piece(int idx, const vector<unsigned char> &b) {
    fstream f(data_path.c_str(), ios::in | ios::out | ios::binary);
    if (!f) { ofstream init(data_path.c_str(), ios::binary); init.close(); f.open(data_path.c_str(), ios::in | ios::out | ios::binary); }
    f.seekp(1LL * idx * meta.piece_length); f.write((const char*)b.data(), b.size()); f.flush();
  }
  bool verify_piece(int idx) {
    vector<unsigned char> b = read_piece(idx);
    return (int)b.size() == piece_size(idx) && sha1_hex(b) == meta.pieces[idx];
  }
  void save() {
    ofstream o(state_path.c_str(), ios::binary);
    for (bool h : have) o << (h ? '1' : '0');
    o << "\n";
  }
  void load_and_verify(bool scan_all = false) {
    string bits; { ifstream in(state_path.c_str(), ios::binary); getline(in, bits); }
    for (int i = 0; i < meta.piece_count(); ++i) {
      bool claimed = i < (int)bits.size() && bits[i] == '1';
      have[i] = (scan_all || claimed) && verify_piece(i);
    }
    save();
  }
  string bitfield() {
    lock_guard<mutex> lk(mu); string b; for (bool h : have) b.push_back(h ? '1' : '0'); return b;
  }
  bool complete() {
    lock_guard<mutex> lk(mu); return all_of(have.begin(), have.end(), [](bool v){ return v; });
  }
  int count_have() {
    lock_guard<mutex> lk(mu); return (int)count(have.begin(), have.end(), true);
  }
};

struct Peer { string host, id; int port = 0; vector<bool> pieces; long long useful_bytes = 0; };

static vector<Peer> parse_peers(const string &body, const string &self) {
  vector<Peer> peers; size_t p = 0;
  while ((p = body.find("\"peer_id\"", p)) != string::npos) {
    size_t b = body.rfind("{", p);
    size_t e = body.find("}", p);
    if (b == string::npos || e == string::npos) break;
    string obj = body.substr(b, e - b + 1); p = e + 1;
    Peer peer;
    auto val = [&](const string &k)->string{
      string pat = "\"" + k + "\":";
      size_t a = obj.find(pat); if (a == string::npos) return "";
      a += pat.size();
      while (a < obj.size() && isspace((unsigned char)obj[a])) a++;
      if (obj[a] == '"') { size_t b = obj.find('"', a + 1); return obj.substr(a + 1, b - a - 1); }
      size_t b = obj.find_first_of(",}", a); return obj.substr(a, b - a);
    };
    peer.id = trim(val("peer_id")); peer.host = trim(val("host")); peer.port = atoi(trim(val("port")).c_str());
    if (!peer.id.empty() && peer.id != self && peer.port > 0) peers.push_back(peer);
  }
  return peers;
}
static vector<Peer> announce(const TorrentMeta &m, const string &peer_id, int port, const string &event) {
  string url = m.announce;
  string prefix = "http://"; if (url.find(prefix) == 0) url = url.substr(prefix.size());
  size_t slash = url.find('/'); string hp = slash == string::npos ? url : url.substr(0, slash);
  string path = slash == string::npos ? "/announce" : url.substr(slash);
  size_t colon = hp.find(':'); string host = colon == string::npos ? hp : hp.substr(0, colon);
  int tracker_port = colon == string::npos ? 80 : atoi(hp.substr(colon + 1).c_str());
  string q = path + "?info_hash=" + urlenc(m.info_hash) + "&peer_id=" + urlenc(peer_id) + "&port=" + to_string(port) + "&event=" + event;
  socket_t s = connect_tcp(host, tracker_port);
  if (s == INVALID_SOCKET) return {};
  string req = "GET " + q + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
  send_all(s, req);
  string data, tmp; char buf[4096]; int n;
  while ((n = recv(s, buf, sizeof(buf), 0)) > 0) data.append(buf, buf + n);
  CLOSESOCK(s);
  size_t body = data.find("\r\n\r\n"); if (body != string::npos) data = data.substr(body + 4);
  return parse_peers(data, peer_id);
}

struct Server {
  State &st; string peer_id; int port; atomic<bool> stop{false}; thread th;
  Server(State &s, const string &pid, int p): st(s), peer_id(pid), port(p) {}
  void start() { th = thread([&]{ run(); }); th.detach(); }
  void join() { stop = true; if (th.joinable()) th.join(); }
  void client(socket_t c) {
    string line;
    if (!read_line(c, line)) { CLOSESOCK(c); return; }
    auto parts = split(line, ' ');
    if (parts.size() < 3 || parts[0] != "HANDSHAKE" || parts[1] != st.meta.info_hash) { CLOSESOCK(c); return; }
    send_all(c, "HANDSHAKE_OK " + peer_id + " " + st.bitfield() + "\nUNCHOKE\n");
    bool interested = false;
    while (read_line(c, line)) {
      auto p = split(line, ' ');
      if (p.empty()) continue;
      if (p[0] == "INTERESTED") interested = true;
      else if (p[0] == "NOT_INTERESTED") interested = false;
      else if (p[0] == "CANCEL") continue;
      else if (p[0] == "REQUEST" && interested && p.size() >= 4) {
        int idx = atoi(p[1].c_str()), off = atoi(p[2].c_str()), len = atoi(p[3].c_str());
        bool ok = idx >= 0 && idx < st.meta.piece_count();
        { lock_guard<mutex> lk(st.mu); ok = ok && st.have[idx]; }
        if (!ok) { send_all(c, "CHOKE\n"); continue; }
        vector<unsigned char> piece = st.read_piece(idx);
        if (off < 0 || len < 0 || off + len > (int)piece.size()) { send_all(c, "CANCEL " + to_string(idx) + " " + to_string(off) + " " + to_string(len) + "\n"); continue; }
        vector<unsigned char> block(piece.begin() + off, piece.begin() + off + len);
        send_all(c, "PIECE " + to_string(idx) + " " + to_string(off) + " " + hex_of(block) + "\n");
      }
    }
    CLOSESOCK(c);
  }
  void run() {
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return;
    int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR || listen(s, 64) == SOCKET_ERROR) { CLOSESOCK(s); return; }
    while (!stop) {
      sockaddr_in ca; socklen_t cl = sizeof(ca);
      socket_t c = accept(s, (sockaddr*)&ca, &cl);
      if (c == INVALID_SOCKET) continue;
      thread(&Server::client, this, c).detach();
    }
    CLOSESOCK(s);
  }
};

struct Scheduler {
  State &st;
  vector<int> availability;
  vector<int> busy;
  map<string, set<int>> failed_by_peer;
  mutex mu;
  Scheduler(State &s): st(s), availability(s.meta.piece_count(), 0), busy(s.meta.piece_count(), 0) {}
  void update_availability(const vector<Peer> &peers) {
    lock_guard<mutex> lk(mu);
    fill(availability.begin(), availability.end(), 0);
    for (auto &p : peers) for (int i = 0; i < (int)p.pieces.size() && i < (int)availability.size(); ++i) if (p.pieces[i]) availability[i]++;
  }
  int pick(const string &peer_key, const vector<bool> &peer_bits) {
    lock_guard<mutex> lk(mu);
    int best = -1, best_av = 1 << 30;
    lock_guard<mutex> lk2(st.mu);
    for (int i = 0; i < st.meta.piece_count(); ++i) {
      if (st.have[i] || busy[i] || i >= (int)peer_bits.size() || !peer_bits[i]) continue;
      if (failed_by_peer[peer_key].count(i)) continue;
      int av = availability[i] > 0 ? availability[i] : 999999;
      if (av < best_av) { best = i; best_av = av; }
    }
    if (best >= 0) busy[best] = 1;
    return best;
  }
  void release(int idx) { if (idx >= 0) { lock_guard<mutex> lk(mu); busy[idx] = 0; } }
  void mark_failed(const string &peer_key, int idx) {
    lock_guard<mutex> lk(mu);
    if (idx >= 0) failed_by_peer[peer_key].insert(idx);
  }
};

static bool download_piece(State &st, socket_t s, int idx) {
  int size = st.piece_size(idx); vector<unsigned char> piece(size);
  int next = 0, completed = 0, outstanding = 0; string line;
  while (completed < size) {
    while (outstanding < PIPELINE && next < size) {
      int len = min(BLOCK_SIZE, size - next);
      if (!send_all(s, "REQUEST " + to_string(idx) + " " + to_string(next) + " " + to_string(len) + "\n")) return false;
      next += len; outstanding++;
    }
    if (!read_line(s, line)) return false;
    auto p = split(line, ' ');
    if (p.size() >= 4 && p[0] == "PIECE") {
      int pi = atoi(p[1].c_str()), off = atoi(p[2].c_str());
      vector<unsigned char> block = unhex(p[3]);
      if (pi != idx || off < 0 || off + (int)block.size() > size) return false;
      copy(block.begin(), block.end(), piece.begin() + off);
      completed += (int)block.size(); outstanding--;
    } else if (p.size() && p[0] == "CHOKE") {
      return false;
    }
  }
  if (sha1_hex(piece) != st.meta.pieces[idx]) return false;
  {
    lock_guard<mutex> lk(st.mu);
    st.write_piece(idx, piece);
    st.have[idx] = true;
    st.save();
  }
  send_all(s, "HAVE " + to_string(idx) + "\n");
  return true;
}

static bool handshake_peer(const TorrentMeta &m, const string &peer_id, Peer &peer) {
  socket_t s = connect_tcp(peer.host, peer.port);
  if (s == INVALID_SOCKET) return false;
  if (!send_all(s, "HANDSHAKE " + m.info_hash + " " + peer_id + "\n")) { CLOSESOCK(s); return false; }
  string line;
  if (!read_line(s, line)) { CLOSESOCK(s); return false; }
  auto p = split(line, ' ');
  if (p.size() < 3 || p[0] != "HANDSHAKE_OK") { CLOSESOCK(s); return false; }
  peer.id = p[1]; peer.pieces.clear(); for (char c : p[2]) peer.pieces.push_back(c == '1');
  CLOSESOCK(s); return true;
}

static void peer_worker(State &st, Scheduler &sched, Peer peer, const string &peer_id, atomic<int> &corrupt_retries) {
  try {
    socket_t s = connect_tcp(peer.host, peer.port);
    if (s == INVALID_SOCKET) return;
    string line;
    if (!send_all(s, "HANDSHAKE " + st.meta.info_hash + " " + peer_id + "\n") || !read_line(s, line)) { CLOSESOCK(s); return; }
    auto hp = split(line, ' ');
    if (hp.size() >= 3 && hp[0] == "HANDSHAKE_OK") {
      peer.pieces.clear(); for (char c : hp[2]) peer.pieces.push_back(c == '1');
    }
    read_line(s, line);
    send_all(s, "INTERESTED\n");
    while (!st.complete()) {
      string peer_key = peer.host + ":" + to_string(peer.port);
      int idx = sched.pick(peer_key, peer.pieces);
      if (idx < 0) { this_thread::sleep_for(chrono::milliseconds(100)); break; }
      bool ok = download_piece(st, s, idx);
      sched.release(idx);
      if (!ok) {
        if (idx >= 0 && idx < (int)peer.pieces.size()) peer.pieces[idx] = false;
        sched.mark_failed(peer_key, idx);
        corrupt_retries++;
      }
    }
    send_all(s, "NOT_INTERESTED\n");
    CLOSESOCK(s);
  } catch (const exception &e) {
    cerr << "peer worker error: " << e.what() << "\n";
    corrupt_retries++;
  }
}

static string peer_id_for(int port) {
  stringstream ss; ss << "GT-" << port << "-" << chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
  return ss.str();
}

static int serve_mode(const string &torrent, const string &data, int port, const string &statefile) {
  TorrentMeta m = load_meta(torrent); State st(m, data, statefile); st.load_and_verify(true);
  string pid = peer_id_for(port);
  Server server(st, pid, port); server.start();
  announce(m, pid, port, "started");
  cerr << "serving " << st.count_have() << "/" << m.piece_count() << " pieces on port " << port << "\n";
  while (true) {
    announce(m, pid, port, "started");
    this_thread::sleep_for(chrono::seconds(5));
  }
  return 0;
}

static int leech_mode(const string &torrent, const string &out, int port, const string &statefile) {
  TorrentMeta m = load_meta(torrent); State st(m, out, statefile); st.load_and_verify();
  string pid = peer_id_for(port);
  Server server(st, pid, port); server.start();
  auto started = chrono::steady_clock::now();
  atomic<int> retries{0};
  while (!st.complete()) {
    vector<Peer> peers = announce(m, pid, port, "started");
    vector<Peer> live;
    for (auto p : peers) if (handshake_peer(m, pid, p)) live.push_back(p);
    cerr << "leecher " << port << " discovered=" << peers.size() << " live=" << live.size()
         << " have=" << st.count_have() << "/" << m.piece_count() << "\n";
    Scheduler sched(st); sched.update_availability(live);
    vector<thread> workers;
    for (auto &p : live) workers.emplace_back(peer_worker, ref(st), ref(sched), p, pid, ref(retries));
    for (auto &t : workers) t.join();
    if (!st.complete()) this_thread::sleep_for(chrono::milliseconds(500));
  }
  auto end = chrono::steady_clock::now();
  announce(m, pid, port, "completed");
  double sec = chrono::duration<double>(end - started).count();
  cout << "{\"event\":\"complete\",\"seconds\":" << fixed << setprecision(3) << sec
       << ",\"bytes\":" << m.length << ",\"pieces\":" << m.piece_count()
       << ",\"corrupt_or_failed_retries\":" << retries.load() << "}\n";
  return 0;
}

static void usage() {
  cerr << "grptorrent create <input> <tracker_url> <torrent_out> [piece_size]\n";
  cerr << "grptorrent seed <torrent> <data_file> <port> <state_file>\n";
  cerr << "grptorrent leech <torrent> <out_file> <port> <state_file>\n";
}

int main(int argc, char **argv) {
  NetInit net;
  try {
    if (argc < 2) { usage(); return 2; }
    string cmd = argv[1];
    if (cmd == "create" && argc >= 5) { create_torrent(argv[2], argv[3], argv[4], argc >= 6 ? atoi(argv[5]) : 65536); return 0; }
    if (cmd == "seed" && argc >= 6) return serve_mode(argv[2], argv[3], atoi(argv[4]), argv[5]);
    if (cmd == "leech" && argc >= 6) return leech_mode(argv[2], argv[3], atoi(argv[4]), argv[5]);
    usage(); return 2;
  } catch (const exception &e) {
    cerr << "error: " << e.what() << "\n"; return 1;
  }
}
