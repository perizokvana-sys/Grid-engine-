// GridEngine - tests/audio.cpp
// Copyright (c) 2026 perizokvana-sys. MIT License, see LICENSE.

#include "gridengine/audio.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace gridengine;
int main() {
    AudioSource source{"assets/tone.wav", true, 0.5F, false};
    assert(source.valid());
    assert(source.set_volume(0.25F));
    assert(!source.set_volume(std::numeric_limits<float>::quiet_NaN()));
    AudioSource loaded;
    assert(AudioSource::deserialize(source.serialize(), loaded));
    assert(loaded.resource == source.resource && loaded.loop && loaded.volume == 0.25F);
    assert(!AudioSource::deserialize("GridAudioSource 9", loaded));
    assert(!AudioSource{"missing.mp3"}.valid());
    AudioManager manager;
    auto id = manager.create(source);
    assert(id != 0 && manager.source_count() == 1);
    assert(manager.play(id) && manager.state(id) == PlaybackState::Playing);
    assert(manager.pause(id) && manager.state(id) == PlaybackState::Paused);
    assert(manager.stop(id) && manager.state(id) == PlaybackState::Stopped);
    auto other = manager.create(AudioSource{"broken.ogg"});
    assert(other != 0); // File/device validation belongs to a decoder backend.
    manager.play(other); manager.remove(other);
    assert(manager.state(other) == PlaybackState::Stopped && manager.source_count() == 1);
    manager.stop_all();
}
