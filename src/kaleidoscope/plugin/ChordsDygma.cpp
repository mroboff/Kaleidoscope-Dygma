/* -*- mode: c++ -*-
 * ChordsDygma -- Combos: several keys pressed together do something else
 * Copyright (C) 2026  Mark Roboff (Keyboard Curator firmware fork)
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "kaleidoscope/plugin/ChordsDygma.h"

#include <Kaleidoscope-EEPROM-Settings.h>
#include <Kaleidoscope-FocusSerial.h>

#include "kaleidoscope/key_events.h"
#include "kaleidoscope/keyswitch_state.h"
#include "kaleidoscope/layers.h"

namespace kaleidoscope {
namespace plugin {

uint16_t Chords::storage_base_ = 0;
bool Chords::enabled_ = false;
uint16_t Chords::timeout_ms_ = Chords::DEFAULT_TIMEOUT_MS;
Chords::Chord Chords::chords_[Chords::MAX_CHORDS];
uint8_t Chords::count_ = 0;

// The map's words, then the timeout, then a word of padding.
static constexpr uint16_t MAP_BYTES = Chords::MAP_WORDS * 2;
static constexpr uint16_t STORAGE_BYTES = MAP_BYTES + 4;
static constexpr uint16_t EEPROM_BYTES = 8192;
static constexpr uint16_t ERASED = 0xFFFF;

void Chords::reserve_storage() {
  if (::EEPROMSettings.used() + STORAGE_BYTES > EEPROM_BYTES) {
    enabled_ = false;
    return;
  }
  storage_base_ = ::EEPROMSettings.requestSlice(STORAGE_BYTES);
  enabled_ = true;
  load();
}

void Chords::load() {
  static uint16_t map[MAP_WORDS];
  Runtime.storage().get(storage_base_, map);
  parse(map);
  uint16_t timeout;
  Runtime.storage().get(storage_base_ + MAP_BYTES, timeout);
  timeout_ms_ = (timeout == ERASED || timeout == 0) ? DEFAULT_TIMEOUT_MS : timeout;
}

// Reads the chords out of a map, stopping at the end marker, at anything
// malformed, or when the tables are full.
void Chords::parse(const uint16_t *map) {
  count_ = 0;
  uint16_t pos = 0;
  while (pos < MAP_WORDS && count_ < MAX_CHORDS) {
    uint16_t result = map[pos];
    if (result == 0 || result == ERASED) {
      break;
    }
    if (pos + 3 >= MAP_WORDS) {
      break;
    }
    Chord &chord = chords_[count_];
    chord.result = result;
    chord.layers = map[pos + 1];
    chord.timeout = map[pos + 2];
    chord.count = 0;
    pos += 3;
    bool closed = false;
    while (pos < MAP_WORDS) {
      uint16_t key = map[pos++];
      if (key == 0) {
        closed = true;
        break;
      }
      if (key == ERASED || key > 255) {
        return;
      }
      if (chord.count < MAX_KEYS) {
        chord.keys[chord.count++] = static_cast<uint8_t>(key - 1);
      }
    }
    if (!closed) {
      return;
    }
    // A chord of one key is a plain key; of none, nothing.
    if (chord.count >= 2) {
      count_++;
    }
  }
}

void Chords::send_map() {
  for (uint8_t i = 0; i < count_; i++) {
    const Chord &chord = chords_[i];
    ::Focus.send(chord.result, chord.layers, chord.timeout);
    for (uint8_t k = 0; k < chord.count; k++) {
      ::Focus.send(static_cast<uint16_t>(chord.keys[k] + 1));
    }
    ::Focus.send(static_cast<uint16_t>(0));
  }
  ::Focus.send(static_cast<uint16_t>(0));
}

void Chords::read_map() {
  static uint16_t map[MAP_WORDS];
  uint16_t pos = 0;
  while (!::Focus.isEOL() && pos < MAP_WORDS) {
    ::Focus.read(map[pos++]);
  }
  // Anything beyond the room is dropped rather than half-kept.
  while (!::Focus.isEOL()) {
    uint16_t discard;
    ::Focus.read(discard);
  }
  for (; pos < MAP_WORDS; pos++) {
    map[pos] = 0;
  }
  Runtime.storage().put(storage_base_, map);
  Runtime.storage().commit();
  parse(map);
}

bool Chords::on_layer(const Chord &chord, uint8_t layer) {
  return chord.layers == 0 || (layer < 16 && (chord.layers & (1u << layer)) != 0);
}

bool Chords::has_key(const Chord &chord, uint8_t position) {
  for (uint8_t k = 0; k < chord.count; k++) {
    if (chord.keys[k] == position) {
      return true;
    }
  }
  return false;
}

uint16_t Chords::timeout_of(const Chord &chord) {
  return chord.timeout == 0 ? timeout_ms_ : chord.timeout;
}

bool Chords::in_queue(KeyAddr addr) const {
  for (uint8_t i = 0; i < queued_; i++) {
    if (queue_[i].addr == addr) {
      return true;
    }
  }
  return false;
}

// Whether every queued key belongs to the chord.
bool Chords::queue_within(const Chord &chord) const {
  for (uint8_t i = 0; i < queued_; i++) {
    if (!has_key(chord, static_cast<uint8_t>(queue_[i].addr.toInt()))) {
      return false;
    }
  }
  return true;
}

// The active chord holding `addr`, with the key's index in it.
int8_t Chords::active_holding(KeyAddr addr, uint8_t &index) const {
  uint8_t position = static_cast<uint8_t>(addr.toInt());
  for (uint8_t a = 0; a < active_count_; a++) {
    const Chord &chord = chords_[active_[a].chord];
    for (uint8_t k = 0; k < chord.count; k++) {
      if (chord.keys[k] == position && (active_[a].held & (1u << k)) != 0) {
        index = k;
        return static_cast<int8_t>(a);
      }
    }
  }
  return -1;
}

void Chords::inject(Key key, KeyAddr addr, uint8_t state) {
  flushing_ = true;
  handleKeyswitchEvent(key, addr, state);
  flushing_ = false;
}

void Chords::push(KeyAddr addr, Key key) {
  if (queued_ == 0) {
    first_pressed_ = Runtime.millisAtCycleStart();
  }
  if (queued_ < MAX_KEYS) {
    queue_[queued_].addr = addr;
    queue_[queued_].key = key;
    queued_++;
  }
}

// Lets every held-back press through, in order, as the ordinary key.
void Chords::flush() {
  uint8_t n = queued_;
  queued_ = 0;
  for (uint8_t i = 0; i < n; i++) {
    inject(queue_[i].key, queue_[i].addr, IS_PRESSED);
  }
}

void Chords::fire(uint8_t index) {
  const Chord &chord = chords_[index];
  queued_ = 0;
  if (active_count_ >= MAX_ACTIVE) {
    return;
  }
  Active &active = active_[active_count_++];
  active.chord = index;
  active.held = static_cast<uint8_t>((1u << chord.count) - 1);
  active.addr = KeyAddr(chord.keys[0]);
  inject(Key(chord.result), active.addr, IS_PRESSED);
}

// With the queue as it is: fire a chord it completes when no longer chord
// could still complete, otherwise keep waiting.
void Chords::decide() {
  if (queued_ == 0) {
    return;
  }
  uint8_t layer = Layer.mostRecent();
  int8_t complete = -1;
  bool longer = false;
  for (uint8_t i = 0; i < count_; i++) {
    const Chord &chord = chords_[i];
    if (!on_layer(chord, layer) || !queue_within(chord)) {
      continue;
    }
    if (chord.count == queued_) {
      if (complete < 0) {
        complete = static_cast<int8_t>(i);
      }
    } else if (chord.count > queued_) {
      longer = true;
    }
  }
  if (complete >= 0 && !longer) {
    fire(static_cast<uint8_t>(complete));
  } else if (complete < 0 && !longer) {
    flush();
  }
}

EventHandlerResult Chords::onKeyswitchEvent(Key &mapped_key, KeyAddr key_addr, uint8_t key_state) {
  if (!enabled_ || count_ == 0 || flushing_) {
    return EventHandlerResult::OK;
  }
  if (!key_addr.isValid() || (key_state & INJECTED) != 0) {
    return EventHandlerResult::OK;
  }
  uint8_t position = static_cast<uint8_t>(key_addr.toInt());
  uint8_t index = 0;
  int8_t holder = active_holding(key_addr, index);

  if (keyToggledOn(key_state)) {
    if (holder >= 0) {
      return EventHandlerResult::EVENT_CONSUMED;
    }
    uint8_t layer = Layer.mostRecent();
    if (queued_ > 0) {
      // Does any chord still fit the queue with this key added?
      bool fits = false;
      for (uint8_t i = 0; i < count_ && !fits; i++) {
        const Chord &chord = chords_[i];
        fits = on_layer(chord, layer) && queue_within(chord) && has_key(chord, position) &&
               chord.count > queued_;
      }
      if (!fits) {
        flush();
      }
    }
    if (queued_ == 0) {
      bool member = false;
      for (uint8_t i = 0; i < count_ && !member; i++) {
        member = on_layer(chords_[i], layer) && has_key(chords_[i], position);
      }
      if (!member) {
        return EventHandlerResult::OK;
      }
    }
    push(key_addr, mapped_key);
    decide();
    return EventHandlerResult::EVENT_CONSUMED;
  }

  if (keyToggledOff(key_state)) {
    if (in_queue(key_addr)) {
      // Released before the chord completed: these were ordinary presses.
      flush();
      return EventHandlerResult::OK;
    }
    if (holder >= 0) {
      Active &active = active_[holder];
      const Chord &chord = chords_[active.chord];
      bool first_release = active.held == static_cast<uint8_t>((1u << chord.count) - 1);
      active.held &= static_cast<uint8_t>(~(1u << index));
      if (first_release) {
        inject(Key(chord.result), active.addr, WAS_PRESSED);
      }
      if (active.held == 0) {
        for (uint8_t a = static_cast<uint8_t>(holder); a + 1 < active_count_; a++) {
          active_[a] = active_[a + 1];
        }
        active_count_--;
      }
      return EventHandlerResult::EVENT_CONSUMED;
    }
    return EventHandlerResult::OK;
  }

  // Held: a key waiting in the queue, or one a chord took, stays silent.
  if (in_queue(key_addr) || holder >= 0) {
    return EventHandlerResult::EVENT_CONSUMED;
  }
  return EventHandlerResult::OK;
}

EventHandlerResult Chords::beforeReportingState() {
  if (!enabled_) {
    return EventHandlerResult::OK;
  }
  if (queued_ > 0) {
    // The timeout is the longest of the chords still possible.
    uint8_t layer = Layer.mostRecent();
    uint16_t timeout = 0;
    int8_t complete = -1;
    for (uint8_t i = 0; i < count_; i++) {
      const Chord &chord = chords_[i];
      if (!on_layer(chord, layer) || !queue_within(chord) || chord.count < queued_) {
        continue;
      }
      if (timeout_of(chord) > timeout) {
        timeout = timeout_of(chord);
      }
      if (chord.count == queued_ && complete < 0) {
        complete = static_cast<int8_t>(i);
      }
    }
    if (Runtime.hasTimeExpired(first_pressed_, timeout)) {
      if (complete >= 0) {
        fire(static_cast<uint8_t>(complete));
      } else {
        flush();
      }
    }
  }
  // A fired chord's result stays pressed while its keys are held.
  for (uint8_t a = 0; a < active_count_; a++) {
    const Chord &chord = chords_[active_[a].chord];
    if (active_[a].held == static_cast<uint8_t>((1u << chord.count) - 1)) {
      inject(Key(chord.result), active_[a].addr, IS_PRESSED | WAS_PRESSED);
    }
  }
  return EventHandlerResult::OK;
}

EventHandlerResult Chords::onFocusEvent(const char *command) {
  if (!enabled_) {
    return EventHandlerResult::OK;
  }
  if (::Focus.handleHelp(command, PSTR("chords.map\nchords.timeout\nchords.memory")))
    return EventHandlerResult::OK;

  if (strncmp_P(command, PSTR("chords."), 7) != 0)
    return EventHandlerResult::OK;

  if (strcmp_P(command + 7, PSTR("map")) == 0) {
    if (::Focus.isEOL()) {
      send_map();
    } else {
      read_map();
    }
    return EventHandlerResult::EVENT_CONSUMED;
  }
  if (strcmp_P(command + 7, PSTR("timeout")) == 0) {
    if (::Focus.isEOL()) {
      ::Focus.send(timeout_ms_);
    } else {
      uint16_t timeout = 0;
      ::Focus.read(timeout);
      if (timeout == 0 || timeout == ERASED) {
        timeout = DEFAULT_TIMEOUT_MS;
      }
      timeout_ms_ = timeout;
      Runtime.storage().put(storage_base_ + MAP_BYTES, timeout);
      Runtime.storage().commit();
    }
    return EventHandlerResult::EVENT_CONSUMED;
  }
  if (strcmp_P(command + 7, PSTR("memory")) == 0) {
    ::Focus.send(MAP_BYTES);
    return EventHandlerResult::EVENT_CONSUMED;
  }
  return EventHandlerResult::OK;
}

}  // namespace plugin
}  // namespace kaleidoscope

kaleidoscope::plugin::Chords Chords;
