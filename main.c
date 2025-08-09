// main.c - Morse Code Decoder with real-time audio analysis
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <fftw3.h>

// --- Constants ---
#define SAMPLE_RATE 44100
#define CHUNK_SIZE 1024
#define FFT_SIZE 2048
#define MAX_FREQUENCY 20000.0
#define DECODED_BUFFER_SIZE 1024
#define WATERFALL_HEIGHT 200
#define WATERFALL_WIDTH 800
#define WATERFALL_LINE_HEIGHT 1
#define FONT_SIZE 24

// --- Morse Code Mapping ---
// This array maps a character's index to its Morse code string.
// It's not a direct character-to-string map, but a lookup table.
// We will use a function to do the reverse lookup.
const char *morse_code_map[] = {
    "-----", // 0
    ".----", // 1
    "..---", // 2
    "...--", // 3
    "....-", // 4
    ".....", // 5
    "-....", // 6
    "--...", // 7
    "---..", // 8
    "----.", // 9
    ".-",    // A
    "-...",  // B
    "-.-.",  // C
    "-..",   // D
    ".",     // E
    "..-.",  // F
    "--.",   // G
    "....",  // H
    "..",    // I
    ".---",  // J
    "-.-",   // K
    ".-..",  // L
    "--",    // M
    "-.",    // N
    "---",   // O
    ".--.",  // P
    "--.-",  // Q
    ".-.",   // R
    "...",   // S
    "-",     // T
    "..-",   // U
    "...-",  // V
    ".--",   // W
    "-..-",  // X
    "-.--",  // Y
    "--..",  // Z
    "..--..", // ?
    "-.-.--", // !
    ".-.-.-", // .
    "--..-.", // ,
    "---...", // :
    "-.-.-.", // ;
    "-.--.",  // (
    "-.--.-", // )
    "---...-", // =
    ".--.-.", // @
    "---..-", // +
    "/",      // special character for word break
    "" // Placeholder for space
};

const int MORSE_MAP_SIZE = sizeof(morse_code_map) / sizeof(morse_code_map[0]);

// Helper function to map Morse code string to a character
char morse_to_char(const char *morse) {
    for (int i = 0; i < MORSE_MAP_SIZE; i++) {
        if (strcmp(morse, morse_code_map[i]) == 0) {
            if (i >= 0 && i <= 9) return '0' + i;
            if (i >= 10 && i <= 35) return 'A' + (i - 10);
            switch (i) {
                case 36: return '?';
                case 37: return '!';
                case 38: return '.';
                case 39: return ',';
                case 40: return ':';
                case 41: return ';';
                case 42: return '(';
                case 43: return ')';
                case 44: return '=';
                case 45: return '@';
                case 46: return '+';
                case 47: return ' ';
            }
        }
    }
    return '?'; // Return '?' for unknown morse sequences
}

// --- Global variables for audio processing ---
SDL_AudioDeviceID deviceId = 0;
SDL_AudioSpec audioSpec;
int16_t audioBuffer[CHUNK_SIZE * 2]; // Twice the chunk size to prevent overflow
int audioBufferPosition = 0;
SDL_mutex *audioMutex = NULL;

// --- FFTW3 variables ---
fftw_complex *fft_in;
fftw_complex *fft_out;
fftw_plan fft_plan;

// --- UI and State variables ---
SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
TTF_Font *font = NULL;
int tunedFrequency = 3000; // Initial center frequency for the filter, now set to 3000Hz
int filterBandwidth = 500; // Bandwidth in Hz, updated to be wider
float gain = 1.0f; // Global gain control
float waterfallBrightness = 10.0f; // Brightness scaling for waterfall display
double squelchThreshold = 200000.0; // Squelch to ignore low-level noise
double squelchDisplayThreshold = 5000.0; // New display squelch to clean up the waterfall
int displayMinFreq = 0;
int displayMaxFreq = 6000; // New adjustable frequency display range, set to 6000Hz
char decodedText[DECODED_BUFFER_SIZE] = {0};
char currentMorse[32] = {0};
int debugMode = 0; // Toggled by 'T' key for troubleshooting

// Waterfall display buffer
SDL_Color waterfallBuffer[WATERFALL_HEIGHT][WATERFALL_WIDTH];

// Morse code decoding state
Uint32 signalStart = 0;
Uint32 lastSignalEnd = 0;
int isSignalPresent = 0;
Uint32 dotDuration = 100; // Initial guess for dot duration in ms

// --- Audio Callback Function ---
// This function is called by SDL when it needs more audio data.
void audioCallback(void *userdata, Uint8 *stream, int len) {
    if (audioMutex) {
        SDL_LockMutex(audioMutex);
        int bytesToCopy = len;
        if (audioBufferPosition + bytesToCopy / sizeof(int16_t) > CHUNK_SIZE * 2) {
             bytesToCopy = (CHUNK_SIZE * 2 - audioBufferPosition) * sizeof(int16_t);
        }
        memcpy(audioBuffer + audioBufferPosition, stream, bytesToCopy);
        audioBufferPosition += bytesToCopy / sizeof(int16_t);
        SDL_UnlockMutex(audioMutex);
    }
}

// --- Function to update the waterfall display ---
void updateWaterfall(double *spectrum) {
    // Scroll the waterfall buffer up by one line
    memmove(waterfallBuffer, waterfallBuffer + 1, (WATERFALL_HEIGHT - 1) * WATERFALL_WIDTH * sizeof(SDL_Color));

    // Find the maximum magnitude in the current spectrum for dynamic scaling
    double maxMagnitude = 1.0;
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        if (spectrum[i] > maxMagnitude) {
            maxMagnitude = spectrum[i];
        }
    }
    
    // Calculate filter boundaries in pixel space
    double lowerBoundFreq = tunedFrequency - (filterBandwidth / 2.0);
    double upperBoundFreq = tunedFrequency + (filterBandwidth / 2.0);
    int lowerBoundX = (int)((lowerBoundFreq - displayMinFreq) / (double)(displayMaxFreq - displayMinFreq) * WATERFALL_WIDTH);
    int upperBoundX = (int)((upperBoundFreq - displayMinFreq) / (double)(displayMaxFreq - displayMinFreq) * WATERFALL_WIDTH);


    // Draw the new line at the bottom
    for (int i = 0; i < WATERFALL_WIDTH; i++) {
        // Calculate the corresponding frequency for this pixel
        double freq_to_render = displayMinFreq + (double)i / WATERFALL_WIDTH * (displayMaxFreq - displayMinFreq);
        int fft_index = (int)(freq_to_render / (SAMPLE_RATE / FFT_SIZE));

        SDL_Color color = {0, 0, 0, 255}; // Default to black for squelch

        // Check for valid fft_index before accessing spectrum array
        if (fft_index >= 0 && fft_index < FFT_SIZE / 2) {
            // Get the magnitude for the frequency and apply gain
            double magnitude = spectrum[fft_index] * gain;
            
            // Apply squelch to the display
            if (magnitude >= squelchDisplayThreshold) {
                // Scale the color value dynamically based on the maximum magnitude
                Uint8 color_value = (Uint8)(fmin(1.0, magnitude / maxMagnitude * waterfallBrightness) * 255.0);

                // Get the current color
                color.r = color_value; // Red channel for signal strength

                // Check if this frequency is within our filter band
                double center_freq_index = (double)tunedFrequency / (SAMPLE_RATE / FFT_SIZE);
                double bandwidth_index = (double)filterBandwidth / (SAMPLE_RATE / FFT_SIZE);
                if (fft_index >= center_freq_index - bandwidth_index / 2 && fft_index <= center_freq_index + bandwidth_index / 2) {
                    // Make the filter window brighter
                    color.g = color_value;
                }
            }
        }
        
        // Draw the filter bounds as white lines
        if (i == lowerBoundX || i == upperBoundX) {
            color.r = 255;
            color.g = 255;
            color.b = 255;
        }

        waterfallBuffer[WATERFALL_HEIGHT - 1][i] = color;
    }
}

// --- Function to render the waterfall display ---
void renderWaterfall() {
    for (int y = 0; y < WATERFALL_HEIGHT; y++) {
        for (int x = 0; x < WATERFALL_WIDTH; x++) {
            SDL_SetRenderDrawColor(renderer, waterfallBuffer[y][x].r, waterfallBuffer[y][x].g, waterfallBuffer[y][x].b, 255);
            SDL_RenderDrawPoint(renderer, x, y);
        }
    }
}

// --- Function to render text ---
void renderText(const char *text, int x, int y) {
    if (!font) return;
    SDL_Color color = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Solid(font, text, color);
    if (surface == NULL) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL) {
        SDL_FreeSurface(surface);
        return;
    }
    SDL_Rect dstrect = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dstrect);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

// --- Morse code decoding logic ---
void updateMorseDecoder(double *spectrum) {
    Uint32 now = SDL_GetTicks();
    double centerFreqBin = (double)tunedFrequency / (SAMPLE_RATE / FFT_SIZE);
    double bandwidthBins = (double)filterBandwidth / (SAMPLE_RATE / FFT_SIZE);
    double magnitude = 0;

    for (int i = (int)(centerFreqBin - bandwidthBins / 2); i < (int)(centerFreqBin + bandwidthBins / 2); i++) {
        if (i >= 0 && i < FFT_SIZE / 2) {
            magnitude += spectrum[i];
        }
    }

    if (debugMode) {
        printf("Magnitude: %.2f, Signal State: %d, Dot Duration: %u\n", magnitude, isSignalPresent, dotDuration);
    }
    
    int currentSignalState = (magnitude * gain > squelchThreshold); // Threshold with gain and squelch

    if (currentSignalState && !isSignalPresent) {
        // Start of a new signal
        isSignalPresent = 1;
        signalStart = now;
        if (lastSignalEnd > 0) {
            Uint32 gapDuration = now - lastSignalEnd;
            if (gapDuration > dotDuration * 3) {
                // End of a word
                strcat(decodedText, " ");
                currentMorse[0] = '\0';
            } else if (gapDuration > dotDuration) {
                // End of a character
                if (strlen(currentMorse) > 0) {
                    char c = morse_to_char(currentMorse);
                    char temp[2] = {c, '\0'};
                    strcat(decodedText, temp);
                    currentMorse[0] = '\0';
                }
            }
        }
    } else if (!currentSignalState && isSignalPresent) {
        // End of a signal
        isSignalPresent = 0;
        lastSignalEnd = now;
        Uint32 signalDuration = now - signalStart;
        if (signalDuration > dotDuration * 1.5) {
            strcat(currentMorse, "-");
        } else {
            strcat(currentMorse, ".");
        }
    }

    if (now - lastSignalEnd > dotDuration * 3 && !isSignalPresent) {
         if (strlen(currentMorse) > 0) {
            char c = morse_to_char(currentMorse);
            char temp[2] = {c, '\0'};
            strcat(decodedText, temp);
            currentMorse[0] = '\0';
        }
    }
}

// --- Main Function ---
int main(int argc, char *argv[]) {
    // --- Initialization ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init Error: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    // --- Create Window and Renderer ---
    window = SDL_CreateWindow("Morse Code Decoder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WATERFALL_WIDTH, 500, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // --- Load Font ---
    font = TTF_OpenFont("DejaVuSansMono.ttf", FONT_SIZE);
    if (!font) {
        fprintf(stderr, "TTF_OpenFont Error: %s\n", TTF_GetError());
        // Fallback to no text rendering if font fails to load
    }

    // --- Audio Setup ---
    audioMutex = SDL_CreateMutex();
    SDL_zero(audioSpec);
    audioSpec.freq = SAMPLE_RATE;
    audioSpec.format = AUDIO_S16;
    audioSpec.channels = 1;
    audioSpec.samples = CHUNK_SIZE;
    audioSpec.callback = audioCallback;
    audioSpec.userdata = NULL;

    // Print audio device name for debugging
    const char* deviceName = SDL_GetAudioDeviceName(0, 1);
    printf("Opening audio device: %s\n", deviceName ? deviceName : "Default Device");
    
    deviceId = SDL_OpenAudioDevice(NULL, 1, &audioSpec, NULL, 0);
    if (deviceId == 0) {
        fprintf(stderr, "Failed to open audio capture device: %s\n", SDL_GetError());
        // Continue without audio input if it fails, just won't be functional.
    } else {
        SDL_PauseAudioDevice(deviceId, 0); // Start recording
    }

    // --- FFTW3 Setup ---
    fft_in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    fft_out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    fft_plan = fftw_plan_dft_1d(FFT_SIZE, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);

    // --- Main Loop ---
    int quit = 0;
    SDL_Event e;
    while (!quit) {
        // --- Event Handling ---
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                quit = 1;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_LEFT:
                        tunedFrequency = fmax(0, tunedFrequency - 10);
                        break;
                    case SDLK_RIGHT:
                        tunedFrequency = fmin(MAX_FREQUENCY, tunedFrequency + 10);
                        break;
                    case SDLK_UP:
                        gain += 0.1f;
                        break;
                    case SDLK_DOWN:
                        gain = fmax(0.1f, gain - 0.1f);
                        break;
                    case SDLK_PAGEUP:
                        displayMaxFreq = fmin(SAMPLE_RATE / 2, displayMaxFreq + 500);
                        break;
                    case SDLK_PAGEDOWN:
                        displayMaxFreq = fmax(displayMinFreq + 500, displayMaxFreq - 500);
                        break;
                    case SDLK_t:
                        debugMode = !debugMode;
                        break;
                    case SDLK_ESCAPE:
                        quit = 1;
                        break;
                }
            }
        }

        // --- Audio Processing and FFT ---
        if (audioMutex) {
            SDL_LockMutex(audioMutex);
            if (audioBufferPosition >= CHUNK_SIZE) {
                // Copy the latest chunk to the FFT input buffer
                for (int i = 0; i < FFT_SIZE; i++) {
                    if (i < CHUNK_SIZE) {
                        fft_in[i][0] = audioBuffer[i] * 0.5 * (1.0 - cos(2 * M_PI * i / (CHUNK_SIZE - 1))); // Apply Hanning window
                        fft_in[i][1] = 0.0;
                    } else {
                        fft_in[i][0] = 0.0;
                        fft_in[i][1] = 0.0;
                    }
                }
                fftw_execute(fft_plan);

                // Calculate magnitude spectrum
                double spectrum[FFT_SIZE / 2];
                for (int i = 0; i < FFT_SIZE / 2; i++) {
                    spectrum[i] = sqrt(fft_out[i][0] * fft_out[i][0] + fft_out[i][1] * fft_out[i][1]);
                }

                // Update UI and decoder
                updateWaterfall(spectrum);
                updateMorseDecoder(spectrum);

                // Shift buffer to process the next chunk
                memmove(audioBuffer, audioBuffer + CHUNK_SIZE, (audioBufferPosition - CHUNK_SIZE) * sizeof(int16_t));
                audioBufferPosition -= CHUNK_SIZE;
            }
            SDL_UnlockMutex(audioMutex);
        }

        // --- Rendering ---
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        renderWaterfall();

        char freqText[64];
        int lowerBound = tunedFrequency - filterBandwidth / 2;
        int upperBound = tunedFrequency + filterBandwidth / 2;
        snprintf(freqText, sizeof(freqText), "Filter: %d - %d Hz (<- ->)", lowerBound, upperBound);
        renderText(freqText, 10, WATERFALL_HEIGHT + 10);

        char gainText[64];
        snprintf(gainText, sizeof(gainText), "Gain: %.1f (Up/Down)", gain);
        renderText(gainText, 10, WATERFALL_HEIGHT + 10 + FONT_SIZE);

        char displayRangeText[64];
        snprintf(displayRangeText, sizeof(displayRangeText), "Display Range: %d - %d Hz (PgUp/PgDn)", displayMinFreq, displayMaxFreq);
        renderText(displayRangeText, 10, WATERFALL_HEIGHT + 10 + FONT_SIZE * 2);

        char debugStatus[64];
        snprintf(debugStatus, sizeof(debugStatus), "Debug Mode: %s (T)", debugMode ? "ON" : "OFF");
        renderText(debugStatus, 10, WATERFALL_HEIGHT + 10 + FONT_SIZE * 3);
        
        char morseText[64];
        snprintf(morseText, sizeof(morseText), "Current Morse: %s", currentMorse);
        renderText(morseText, 10, WATERFALL_HEIGHT + 10 + FONT_SIZE * 4);

        char decodedLabel[64];
        snprintf(decodedLabel, sizeof(decodedLabel), "Decoded Text:");
        renderText(decodedLabel, 10, WATERFALL_HEIGHT + 10 + FONT_SIZE * 5);

        renderText(decodedText, 10, WATERFALL_HEIGHT + 10 + FONT_SIZE * 6);


        SDL_RenderPresent(renderer);
    }

    // --- Cleanup ---
    SDL_PauseAudioDevice(deviceId, 1);
    SDL_CloseAudioDevice(deviceId);
    SDL_DestroyMutex(audioMutex);
    TTF_CloseFont(font);
    fftw_destroy_plan(fft_plan);
    fftw_free(fft_in);
    fftw_free(fft_out);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
