# Morse Decoder

Morse Decoder is a real-time audio application that visualizes the frequency spectrum of incoming audio and decodes Morse code.

## Build

### Linux
Run the configuration script to check for dependencies and then build:

```bash
./configure
make
```

### Windows

Use the Windows makefile:

```bash
make -f Makefile.win
```

## Controls

- **Left/Right Arrows**: Tune the center frequency
- **Up/Down Arrows**: Adjust gain
- **PageUp/PageDown**: Change displayed frequency range
- **Esc**: Exit the application

## Roadmap

- Packaging for easier distribution
- Improved Morse decoding accuracy
- Cross-platform UI enhancements
- Network-based audio input
