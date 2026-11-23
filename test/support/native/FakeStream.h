#pragma once

// A Stream test double for the native suites: feed() queues bytes for the code
// under test to read, and everything it prints collects in `output` instead of
// going to stdout.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Arduino.h"

class FakeStream : public Stream
{
public:
    std::string output;

    using Stream::write;

    size_t write(uint8_t c) override
    {
        output.push_back(static_cast<char>(c));
        return 1;
    }

    int available() override { return static_cast<int>(_input.size() - _pos); }

    int read() override
    {
        if (_pos >= _input.size()) return -1;
        return static_cast<unsigned char>(_input[_pos++]);
    }

    int peek() override
    {
        if (_pos >= _input.size()) return -1;
        return static_cast<unsigned char>(_input[_pos]);
    }

    /// Queues text for read().
    void feed(const std::string &text) { _input.append(text); }

    /// Returns the collected output and clears it.
    std::string take()
    {
        std::string out;
        out.swap(output);
        return out;
    }

    /// The collected output split at line feeds, without them; cleared afterwards.
    std::vector<std::string> takeLines()
    {
        std::vector<std::string> lines;
        std::string text = take();
        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            start = end + 1;
        }
        return lines;
    }

private:
    std::string _input;
    size_t _pos = 0;
};
