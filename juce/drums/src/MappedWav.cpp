#include "MappedWav.h"

#if JUCE_LINUX || JUCE_MAC || JUCE_BSD
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
 #define ALBION_HAS_MMAP 1
#else
 #define ALBION_HAS_MMAP 0
#endif

namespace
{
    std::uint32_t readU32 (const std::uint8_t* p) noexcept { return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24); }
    std::uint16_t readU16 (const std::uint8_t* p) noexcept { return (std::uint16_t) (p[0] | (p[1] << 8)); }
}

MappedWav::~MappedWav()
{
   #if ALBION_HAS_MMAP
    if (isMapped && base != nullptr)
        munmap (base, size);
   #endif
}

bool MappedWav::open (const juce::File& file, juce::String& error)
{
   #if ALBION_HAS_MMAP
    const int fd = ::open (file.getFullPathName().toRawUTF8(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        error = "Can't open " + file.getFullPathName();
        return false;
    }

    struct stat st {};
    if (fstat (fd, &st) != 0 || st.st_size < 44)
    {
        ::close (fd);
        error = "Not a valid WAV file: " + file.getFullPathName();
        return false;
    }

    size = (size_t) st.st_size;
    base = mmap (nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close (fd);   // the mapping stays valid without the descriptor

    if (base == MAP_FAILED)
    {
        base = nullptr;
        error = "Can't map " + file.getFullPathName();
        return false;
    }
    isMapped = true;
   #else
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 44)
    {
        error = "Can't read " + file.getFullPathName();
        return false;
    }
    size = mb.getSize();
    heapCopy.malloc (size);
    std::memcpy (heapCopy.get(), mb.getData(), size);
    base = heapCopy.get();
   #endif

    if (! parse (error))
    {
        error << " (" << file.getFullPathName() << ")";
        return false;
    }
    return true;
}

bool MappedWav::parse (juce::String& error)
{
    const auto* p   = static_cast<const std::uint8_t*> (base);
    const auto* end = p + size;

    if (std::memcmp (p, "RIFF", 4) != 0 || std::memcmp (p + 8, "WAVE", 4) != 0)
    {
        error = "Not a RIFF/WAVE file";
        return false;
    }

    bool haveFmt = false;
    int formatTag = 0, bits = 0;
    const std::uint8_t* data = nullptr;
    std::uint64_t dataSize = 0;

    for (auto* c = p + 12; c + 8 <= end;)
    {
        const auto chunkSize = (std::uint64_t) readU32 (c + 4);
        const auto* body = c + 8;

        if (std::memcmp (c, "fmt ", 4) == 0 && body + 16 <= end)
        {
            formatTag   = readU16 (body);
            numChannels = readU16 (body + 2);
            sampleRate  = (double) readU32 (body + 4);
            bits        = readU16 (body + 14);
            if (formatTag == 0xFFFE && chunkSize >= 40 && body + 26 <= end)
                formatTag = readU16 (body + 24);   // WAVE_FORMAT_EXTENSIBLE sub-format
            haveFmt = true;
        }
        else if (std::memcmp (c, "data", 4) == 0)
        {
            data = body;
            dataSize = juce::jmin (chunkSize, (std::uint64_t) (end - body));
            break;
        }

        c = body + chunkSize + (chunkSize & 1);   // chunks are word aligned
    }

    if (! haveFmt || data == nullptr || numChannels <= 0)
    {
        error = "WAV file is missing its fmt or data chunk";
        return false;
    }

    if (formatTag == 1 && bits == 16)       format = Format::Int16;
    else if (formatTag == 1 && bits == 24)  format = Format::Int24;
    else if (formatTag == 1 && bits == 32)  format = Format::Int32;
    else if (formatTag == 3 && bits == 32)  format = Format::Float32;
    else
    {
        error = "Unsupported WAV format (tag " + juce::String (formatTag) + ", " + juce::String (bits) + " bit)";
        return false;
    }

    bytesPerSample = bits / 8;
    bytesPerFrame  = bytesPerSample * numChannels;
    frames    = data;
    numFrames = (std::int64_t) (dataSize / (std::uint64_t) bytesPerFrame);
    return true;
}

void MappedWav::adviseWillNeed() const noexcept
{
   #if ALBION_HAS_MMAP
    if (isMapped && base != nullptr)
        madvise (base, size, MADV_WILLNEED);
   #endif
}

void MappedWav::touchHead (size_t bytes) const noexcept
{
    if (frames == nullptr)
        return;

    const auto* start = frames;
    const auto* stop  = juce::jmin (frames + bytes, static_cast<const std::uint8_t*> (base) + size);
    volatile std::uint8_t sink = 0;
    for (auto* q = start; q < stop; q += 4096)
        sink = (std::uint8_t) (sink + *q);
    juce::ignoreUnused (sink);
}
