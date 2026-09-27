#ifndef SNESTICLE_ROMLAB_INPUT_MOVIE_H
#define SNESTICLE_ROMLAB_INPUT_MOVIE_H

#include <stdint.h>
#include <string>
#include <vector>

#include "types.h"
#include "emuinput.h"

class InputMovie
{
public:
    InputMovie();

    bool Load(const std::string &path, std::string *error);
    void Reset();
    const Emu::SysInputT &Get(uint64_t relativeFrame);
    bool Empty() const { return m_Events.empty(); }

private:
    struct Event
    {
        uint64_t Frame;
        Emu::SysInputT Input;
    };

    std::vector<Event> m_Events;
    Emu::SysInputT m_Default;
    Emu::SysInputT m_Current;
    size_t m_Next;
};

#endif
