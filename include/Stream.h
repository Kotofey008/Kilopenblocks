#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>

// Observed hierarchy:
// Stream (base): ByteOrderSerialize(void*, int), SerializeArray(void*, int, int)
// FileStream : Stream — Open(path, Mode), Serialize, Size, Close
// operator<<(Stream&, std::string&)
// Used by Game::SaveGameHeader / LoadGameHeader / SaveGameSettings, GridData::SaveChunk, etc.

class Stream {
public:
    enum class Direction { Read, Write };
    virtual ~Stream() = default;
    virtual bool IsOpen() const = 0;
    virtual void Serialize(void* data, int size) = 0; // direction depends on mode
    void ByteOrderSerialize(void* data, int size);     // endian-aware; observed
    void SerializeArray(void* data, int elemSize, int count);
    // helpers
    void Write(const void* data, int size) { Serialize(const_cast<void*>(data), size); }
    void Read(void* data, int size) { Serialize(data, size); }
    template<typename T> void Ser(T& v) { ByteOrderSerialize(&v, (int)sizeof(T)); }
};

class FileStream : public Stream {
public:
    enum class Mode { Read = 0, Write = 1, ReadWrite = 2 };
    FileStream();
    ~FileStream() override;
    bool Open(const char* path, Mode mode);
    void Close();
    int Size();
    bool IsOpen() const override { return fp_ != nullptr; }
    void Serialize(void* data, int size) override;
private:
    FILE* fp_ = nullptr;
    Mode mode_ = Mode::Read;
};

// string ser observed as free operator<<
Stream& operator<<(Stream& s, std::string& str);
Stream& operator>>(Stream& s, std::string& str);
