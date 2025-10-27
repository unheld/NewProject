# NewProject Synthesizer 🎛️🎶

A JUCE-based experimental monophonic synth with:
- Morphable oscillator (Sine → Triangle → Saw → Square)
- ADSR envelope
- Stereo width & auto-pan
- LFO modulation (pitch → cutoff)
- Drive, Crush, Chaos & Glitch FX
- Delay with feedback and wet/dry control
- MIDI input + on-screen keyboard
- Live oscilloscope (audio waveform view)
- Smooth UI and layout scaling

## Requirements
- **JUCE 8.0.10**
- **Visual Studio 2022 v17.14.18**
- Windows 10/11 (x64)

## Build
Open `NewProject.jucer` in Projucer → Exporter: *Visual Studio 2022* → Build

## Controls & UI
Top strip = synthesis & FX controls  
Middle = oscilloscope  
Bottom = scrollable MIDI keyboard

## MIDI Input
Works with:
- Computer keyboard MIDI keyboard
- External USB devices (auto-enabled)

## Future Roadmap
- Polyphony
- Preset browser
- Improved wave morph visualization
- CPU optimizations on filter + delay

---

### Contact
Made with ❤️ using JUCE.
