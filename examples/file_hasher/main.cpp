// File Hasher - a real quick tool built with SimplyUI.
// Drop files on the window to compute CRC32 + SHA-256 in a background thread.
// Shows: drag & drop, worker threads + sui::Wake, progress (also on the taskbar),
//        tables, clipboard, toasts, native save dialog, settings that persist between
//        runs, and a single instance that takes files dropped onto the .exe.
#define _CRT_SECURE_NO_WARNINGS   // _wfopen is fine here (Visual Studio would ask for _wfopen_s)
#include "simplyui.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------------
// Minimal CRC32 + SHA-256 (public-domain style reference implementations)
// ---------------------------------------------------------------------------------
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    uint64_t bits = 0;
    size_t used = 0;

    static uint32_t Rot(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = Rot(w[i - 15], 7) ^ Rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rot(w[i - 2], 17) ^ Rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void Update(const uint8_t* p, size_t n) {
        bits += (uint64_t)n * 8;
        while (n > 0) {
            size_t take = 64 - used < n ? 64 - used : n;
            memcpy(buf + used, p, take);
            used += take; p += take; n -= take;
            if (used == 64) { Block(buf); used = 0; }
        }
    }
    std::string Final() {
        uint64_t total = bits;
        uint8_t pad = 0x80;
        Update(&pad, 1);
        uint8_t zero = 0;
        while (used != 56) Update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = (uint8_t)(total >> (56 - i * 8));
        Update(len, 8);
        char out[65];
        for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
        return out;
    }
};

static uint32_t Crc32(uint32_t crc, const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ---------------------------------------------------------------------------------
// The tool
// ---------------------------------------------------------------------------------
struct Item {
    std::string path, name, crc, sha;
    long long size = 0;
    bool done = false, failed = false;
};

int main() {
    std::vector<Item> items;
    std::mutex lock;                       // protects `items` while the worker runs
    std::atomic<bool> busy{false}, cancel{false};
    std::atomic<float> progress{0.0f};
    std::thread worker;
    int selected = -1;
    bool uppercase = false;

    // Files passed on the command line (e.g. dropped onto the .exe in Explorer).
    std::vector<std::string> startup = sui::CommandLine();

    auto hashAll = [&]() {
        if (busy) return;
        if (worker.joinable()) worker.join();
        busy = true;
        cancel = false;
        worker = std::thread([&] {
            for (size_t i = 0;; i++) {
                std::string path;
                {
                    std::lock_guard<std::mutex> g(lock);
                    while (i < items.size() && items[i].done) i++;
                    if (i >= items.size()) break;
                    path = items[i].path;
                }
                FILE* f = _wfopen(sui::ToWide(path).c_str(), L"rb");
                Sha256 sha;
                uint32_t crc = 0;
                long long size = 0, total = 0;
                if (f) {
                    fseek(f, 0, SEEK_END);
                    total = _ftelli64(f);
                    fseek(f, 0, SEEK_SET);
                    static uint8_t buf[1 << 16];
                    size_t n;
                    while (!cancel && (n = fread(buf, 1, sizeof(buf), f)) > 0) {
                        sha.Update(buf, n);
                        crc = Crc32(crc, buf, n);
                        size += (long long)n;
                        progress = total > 0 ? (float)size / (float)total : 1.0f;
                        sui::Wake();       // let the UI redraw the progress bar
                    }
                    fclose(f);
                }
                std::lock_guard<std::mutex> g(lock);
                if (cancel) break;
                Item& it = items[i];
                it.done = true;
                it.failed = f == nullptr;
                it.size = size;
                char c[16];
                snprintf(c, sizeof(c), "%08x", crc);
                it.crc = f ? c : "-";
                it.sha = f ? sha.Final() : "could not open file";
            }
            busy = false;
            sui::Wake();
        });
    };

    sui::Config cfg;
    cfg.title = "File Hasher";
    cfg.width = 720;
    cfg.height = 520;
    cfg.rememberWindow = true;
    cfg.singleInstance = true;   // files dropped onto the .exe later go to this window (DroppedFiles)

    int result = sui::Run(cfg, [&] {
        sui::Persist("uppercase", uppercase);
        auto fmt = [&](std::string s) {
            if (uppercase) for (char& ch : s) ch = (char)toupper((unsigned char)ch);
            return s;
        };

        // Files dropped anywhere on the window are added to the list.
        std::vector<std::string> dropped = sui::DropZone("Drop files here to hash them", 80);
        for (const std::string& f : sui::DroppedFiles()) dropped.push_back(f);
        if (!startup.empty()) { dropped.insert(dropped.end(), startup.begin(), startup.end()); startup.clear(); }
        if (!dropped.empty()) {
            std::lock_guard<std::mutex> g(lock);
            for (const std::string& p : dropped) {
                bool exists = false;
                for (const Item& it : items) exists |= it.path == p;
                if (exists) continue;
                Item it;
                it.path = p;
                size_t slash = p.find_last_of("\\/");
                it.name = slash == std::string::npos ? p : p.substr(slash + 1);
                items.push_back(it);
            }
        }
        if (!dropped.empty()) hashAll();

        std::vector<Item> view;
        {
            std::lock_guard<std::mutex> g(lock);
            view = items;
        }

        if (busy) {
            sui::TaskbarProgress(progress);   // also on the taskbar button (gone once we stop calling it)
            sui::ProgressBar(progress);
            sui::SameLine();
            if (sui::Button("Cancel")) cancel = true;
        }

        if (view.empty()) sui::TextDim("No files yet - drop some on the window or click the area above.");
        sui::NextTableWeights({2.2f, 1, 1.1f, 4.5f});
        if (sui::BeginTable("files", {"File", "Size", "CRC32", "SHA-256"}, sui::TableStriped | sui::TableResizable, -1)) {
            for (int i = 0; i < (int)view.size(); i++) {
                const Item& it = view[i];
                if (sui::TableNextRow(i == selected)) {
                    selected = i;
                    if (it.done && !it.failed) {
                        sui::SetClipboard(fmt(it.sha));
                        sui::Toast("SHA-256 copied to clipboard", sui::Level::Success, 2.0f);
                    }
                }
                sui::TableNextCell();
                sui::Text(it.name);
                sui::TableNextCell();
                if (!it.done) sui::TextDim("...");
                else if (it.size < 1024) sui::TextDim(sui::Format("%lld B", it.size));
                else if (it.size < 1048576) sui::TextDim(sui::Format("%.1f KB", it.size / 1024.0));
                else sui::TextDim(sui::Format("%.1f MB", it.size / 1048576.0));
                sui::TableNextCell();
                sui::Text(it.done ? fmt(it.crc) : "");
                sui::TableNextCell();
                if (it.failed) sui::TextColored(sui::GetStyle().danger, it.sha);
                else if (it.done) sui::Text(fmt(it.sha));
                else sui::Spinner(16);
            }
            sui::EndTable();
        }

        sui::BeginFooter();
        sui::BeginHorizontal();
        sui::Checkbox("Uppercase", uppercase);
        if (sui::Button("Copy all")) {
            std::string all;
            for (const Item& it : view)
                if (it.done && !it.failed) all += fmt(it.sha) + "  " + it.name + "\n";
            sui::SetClipboard(all);
            sui::Toast("Copied " + std::to_string(view.size()) + " hashes");
        }
        if (sui::Button("Save .sha256")) {
            std::string path = sui::SaveFileDialog("SHA-256 list|*.sha256|Text|*.txt", "hashes.sha256");
            if (!path.empty()) {
                FILE* f = _wfopen(sui::ToWide(path).c_str(), L"wb");
                if (f) {
                    for (const Item& it : view)
                        if (it.done && !it.failed) fprintf(f, "%s *%s\n", fmt(it.sha).c_str(), it.name.c_str());
                    fclose(f);
                    sui::Toast("Saved", sui::Level::Success);
                }
            }
        }
        if (sui::Button("Clear") && !busy) {
            std::lock_guard<std::mutex> g(lock);
            items.clear();
            selected = -1;
        }
        sui::EndHorizontal();
        sui::EndFooter();
    });

    cancel = true;
    if (worker.joinable()) worker.join();
    return result;
}
