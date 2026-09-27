#include "input_movie.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "snio.h"

namespace {

static std::string Upper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return (char)std::toupper(c); });
    return value;
}

static bool ParseFrame(const std::string &text, uint64_t *value)
{
	if (text.empty() || text[0] == '-')
		return false;
    char *end = NULL;
    unsigned long long parsed = strtoull(text.c_str(), &end, 10);
    if (!end || *end || end == text.c_str())
        return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool ParsePad(const std::string &text, uint16_t *value)
{
    std::string upper = Upper(text);
    if (upper == "-" || upper == "NONE" || upper == "IDLE")
    {
        *value = 0;
        return true;
    }
    if (upper == "OFF" || upper == "DISCONNECTED")
    {
        *value = EMUSYS_DEVICE_DISCONNECTED;
        return true;
    }

    /* Button names A and B are valid hexadecimal digits, so accepting bare
       hex here silently turned a one-button event into masks $000A/$000B.
       Numeric masks are intentionally explicit (`0xNNNN`). */
    bool numeric = upper.rfind("0X", 0) == 0 && upper.size() > 2;
    const size_t start = 2;
    for (size_t i = start; numeric && i < upper.size(); ++i)
    {
        if (!std::isxdigit((unsigned char)upper[i]))
        {
            numeric = false;
            break;
        }
    }
    if (numeric)
    {
        char *end = NULL;
        unsigned long parsed = strtoul(upper.c_str() + start, &end, 16);
        if (end && end != upper.c_str() + start && !*end &&
            parsed <= 0xFFFFu)
        {
            *value = (uint16_t)parsed;
            return true;
        }
    }

    uint16_t mask = 0;
    size_t pos = 0;
    while (pos < upper.size())
    {
        size_t end = upper.find_first_of("+|", pos);
        std::string name = upper.substr(pos,
            end == std::string::npos ? std::string::npos : end - pos);
        uint16_t bit = 0;
        if (name == "A") bit = SNESIO_JOY_A;
        else if (name == "B") bit = SNESIO_JOY_B;
        else if (name == "X") bit = SNESIO_JOY_X;
        else if (name == "Y") bit = SNESIO_JOY_Y;
        else if (name == "L") bit = SNESIO_JOY_L;
        else if (name == "R") bit = SNESIO_JOY_R;
        else if (name == "UP") bit = SNESIO_JOY_UP;
        else if (name == "DOWN") bit = SNESIO_JOY_DOWN;
        else if (name == "LEFT") bit = SNESIO_JOY_LEFT;
        else if (name == "RIGHT") bit = SNESIO_JOY_RIGHT;
        else if (name == "START") bit = SNESIO_JOY_START;
        else if (name == "SELECT") bit = SNESIO_JOY_SELECT;
        else return false;
        mask |= bit;
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }
    *value = mask;
    return true;
}

} // namespace

InputMovie::InputMovie() : m_Next(0)
{
    m_Default.uPad[0] = 0;
    for (size_t i = 1; i < EMUSYS_DEVICE_NUM; ++i)
        m_Default.uPad[i] = EMUSYS_DEVICE_DISCONNECTED;
    m_Current = m_Default;
}

bool InputMovie::Load(const std::string &path, std::string *error)
{
    std::ifstream input(path.c_str());
    if (!input)
    {
        if (error) *error = "cannot open input movie: " + path;
        return false;
    }

    m_Events.clear();
    Emu::SysInputT state = m_Default;
    std::string line;
    uint64_t previousFrame = 0;
    bool havePrevious = false;
    size_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);
        std::replace(line.begin(), line.end(), ',', ' ');

        std::istringstream stream(line);
        std::string frameText;
        if (!(stream >> frameText))
            continue;

        uint64_t frame = 0;
        if (!ParseFrame(frameText, &frame) ||
            (havePrevious && frame < previousFrame))
        {
            if (error)
            {
                std::ostringstream message;
                message << path << ':' << lineNumber
                        << ": invalid or out-of-order frame";
                *error = message.str();
            }
            return false;
        }

        std::string padText;
        size_t pad = 0;
        while (stream >> padText)
        {
            if (pad >= EMUSYS_DEVICE_NUM ||
                !ParsePad(padText, &state.uPad[pad]))
            {
                if (error)
                {
                    std::ostringstream message;
                    message << path << ':' << lineNumber
                            << ": invalid pad value '" << padText << "'";
                    *error = message.str();
                }
                return false;
            }
            ++pad;
        }
        if (!pad)
        {
            if (error)
            {
                std::ostringstream message;
                message << path << ':' << lineNumber
                        << ": frame has no pad state";
                *error = message.str();
            }
            return false;
        }

        Event event;
        event.Frame = frame;
        event.Input = state;
        if (!m_Events.empty() && m_Events.back().Frame == frame)
            m_Events.back() = event;
        else
            m_Events.push_back(event);
        previousFrame = frame;
        havePrevious = true;
    }

    Reset();
    return true;
}

void InputMovie::Reset()
{
    m_Current = m_Default;
    m_Next = 0;
}

const Emu::SysInputT &InputMovie::Get(uint64_t relativeFrame)
{
    while (m_Next < m_Events.size() &&
           m_Events[m_Next].Frame <= relativeFrame)
    {
        m_Current = m_Events[m_Next].Input;
        ++m_Next;
    }
    return m_Current;
}
