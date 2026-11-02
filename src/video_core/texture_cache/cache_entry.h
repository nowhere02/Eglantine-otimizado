// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace VideoCommon {

template <class Slots, class Lookup>
auto EraseCachedSlot(Slots& slots, Lookup& lookup, typename Lookup::iterator entry) {
    slots.erase(entry->second);
    return lookup.erase(entry);
}

} // namespace VideoCommon
