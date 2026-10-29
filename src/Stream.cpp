#include "Stream.h"
#include <cstring>
#include <algorithm>

void Stream::ByteOrderSerialize(void* data, int size) {
    // Observed size 98 — on little-endian Android/x86 this is effectively a plain serialize.
    // Big-endian swap path may exist; for now match LE host behavior.
    Serialize(data, size);
}

void Stream::SerializeArray(void* data, int elemSize, int count) {
    if (elemSize <= 0 || count <= 0) return;
    Serialize(data, elemSize * count);
}

FileStream::FileStream() {}
FileStream::~FileStream() { Close(); }

bool FileStream::Open(const char* path, Mode mode) {
    Close();
    const char* m = "rb";
    if (mode == Mode::Write) m = "wb";
    else if (mode == Mode::ReadWrite) m = "r+b";
    fp_ = std::fopen(path, m);
    mode_ = mode;
    return fp_ != nullptr;
}

void FileStream::Close() {
    if (fp_) { std::fclose(fp_); fp_ = nullptr; }
}

int FileStream::Size() {
    if (!fp_) return 0;
    long cur = std::ftell(fp_);
    std::fseek(fp_, 0, SEEK_END);
    long sz = std::ftell(fp_);
    std::fseek(fp_, cur, SEEK_SET);
    return (int)sz;
}

void FileStream::Serialize(void* data, int size) {
    if (!fp_ || size <= 0) return;
    if (mode_ == Mode::Write)
        std::fwrite(data, 1, (size_t)size, fp_);
    else
        std::fread(data, 1, (size_t)size, fp_);
}

Stream& operator<<(Stream& s, std::string& str) {
    int len = (int)str.size();
    s.Ser(len);
    if (len > 0) s.Serialize(const_cast<char*>(str.data()), len);
    return s;
}

Stream& operator>>(Stream& s, std::string& str) {
    int len = 0;
    s.Ser(len);
    if (len < 0) len = 0;
    str.resize((size_t)len);
    if (len > 0) s.Serialize(&str[0], len);
    return s;
}
