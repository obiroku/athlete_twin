#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <complex>
#include <vector>

namespace lens::test
{
// Pink noise (Paul Kellet's filter), scaled to the given RMS in dBFS.
juce::AudioBuffer<float> makePink (int channels, int numSamples, juce::int64 seed, double rmsDb = -18.0, bool identicalChannels = false);

// Amplitude and phase of a sinusoid at freq, by least squares over
// [start, start + length).
std::complex<double> sineComponent (const float* x, int start, int length, double sampleRate, double freq);
double sineAmplitude (const float* x, int start, int length, double sampleRate, double freq);

// Integrated K-weighted loudness of [start, end) of a buffer (BS.1770,
// no gating): what a steady test signal reads.
double lufsOf (const juce::AudioBuffer<float>&, double sampleRate, int start = 0, int end = -1);

double rmsDb (const juce::AudioBuffer<float>&, int channel, int start = 0, int end = -1);

// Largest |x[n] − x[n−1]| over [start, end) on one channel.
double maxStep (const juce::AudioBuffer<float>&, int channel, int start, int end);

// DFT of a real sequence at one frequency.
std::complex<double> dftAt (const std::vector<double>& h, double sampleRate, double freq);

bool allFinite (const juce::AudioBuffer<float>&);

// A delayed copy (zeros shifted in).
juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>&, int samples);
} // namespace lens::test
