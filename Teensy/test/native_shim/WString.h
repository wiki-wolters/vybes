#ifndef NATIVE_SHIM_WSTRING_H
#define NATIVE_SHIM_WSTRING_H

// Minimal host-side stand-in for the Arduino String class - just the subset
// the firmware sources under test use, with matching semantics.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class String {
public:
    String() {}
    String(const char* s) : s(s ? s : "") {}
    String(const std::string& str) : s(str) {}
    String(char c) : s(1, c) {}
    String(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); s = b; }
    String(unsigned int v) { char b[24]; snprintf(b, sizeof(b), "%u", v); s = b; }
    String(long v) { char b[24]; snprintf(b, sizeof(b), "%ld", v); s = b; }
    String(unsigned long v) { char b[24]; snprintf(b, sizeof(b), "%lu", v); s = b; }
    String(float v, int digits = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", digits, (double)v); s = b; }
    String(double v, int digits = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", digits, v); s = b; }

    unsigned int length() const { return (unsigned int)s.size(); }
    char charAt(unsigned int i) const { return i < s.size() ? s[i] : '\0'; }
    char operator[](unsigned int i) const { return charAt(i); }
    const char* c_str() const { return s.c_str(); }

    int indexOf(char c) const {
        size_t p = s.find(c);
        return p == std::string::npos ? -1 : (int)p;
    }

    String substring(unsigned int from) const {
        if (from >= s.size()) return String();
        return String(s.substr(from));
    }
    String substring(unsigned int from, unsigned int to) const {
        if (from >= to || from >= s.size()) return String();
        if (to > s.size()) to = (unsigned int)s.size();
        return String(s.substr(from, to - from));
    }

    void trim() {
        size_t b = 0, e = s.size();
        while (b < e && isspace((unsigned char)s[b])) b++;
        while (e > b && isspace((unsigned char)s[e - 1])) e--;
        s = s.substr(b, e - b);
    }

    bool equalsIgnoreCase(const String& o) const {
        if (s.size() != o.s.size()) return false;
        for (size_t i = 0; i < s.size(); i++) {
            if (tolower((unsigned char)s[i]) != tolower((unsigned char)o.s[i])) return false;
        }
        return true;
    }

    bool endsWith(const String& suffix) const {
        if (suffix.s.size() > s.size()) return false;
        return s.compare(s.size() - suffix.s.size(), suffix.s.size(), suffix.s) == 0;
    }

    // Arduino's toFloat/toInt return 0 for non-numeric input
    float toFloat() const { return (float)atof(s.c_str()); }
    long toInt() const { return atol(s.c_str()); }

    bool operator==(const String& o) const { return s == o.s; }
    bool operator==(const char* o) const { return s == (o ? o : ""); }
    bool operator!=(const String& o) const { return s != o.s; }

    String& operator+=(char c) { s += c; return *this; }
    String& operator+=(const String& o) { s += o.s; return *this; }
    String& operator+=(const char* o) { if (o) s += o; return *this; }

    friend String operator+(String lhs, const String& rhs) { lhs.s += rhs.s; return lhs; }
    friend String operator+(String lhs, const char* rhs) { if (rhs) lhs.s += rhs; return lhs; }
    friend String operator+(const char* lhs, const String& rhs) { return String(lhs) + rhs; }

private:
    std::string s;
};

#endif // NATIVE_SHIM_WSTRING_H
