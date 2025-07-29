#pragma once

#include <vector>
#include <array>
#include <complex>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <memory>
#include <functional>
#include "../primitives/primitives.hpp"

namespace axonvex::types::signals {

/**
 * @brief Real-time signal processing buffer
 */
template<typename T, size_t Size>
class SignalBuffer {
private:
    std::array<T, Size> buffer_;
    size_t write_index_;
    size_t samples_count_;
    
public:
    SignalBuffer() : write_index_(0), samples_count_(0) {
        buffer_.fill(T{});
    }
    
    void push(const T& sample) {
        buffer_[write_index_] = sample;
        write_index_ = (write_index_ + 1) % Size;
        if (samples_count_ < Size) {
            samples_count_++;
        }
    }
    
    T at(size_t index) const {
        if (index >= samples_count_) return T{};
        size_t actual_index = (write_index_ + Size - samples_count_ + index) % Size;
        return buffer_[actual_index];
    }
    
    T latest() const {
        if (samples_count_ == 0) return T{};
        size_t latest_index = (write_index_ + Size - 1) % Size;
        return buffer_[latest_index];
    }
    
    size_t size() const { return samples_count_; }
    size_t capacity() const { return Size; }
    bool full() const { return samples_count_ == Size; }
    
    void clear() {
        write_index_ = 0;
        samples_count_ = 0;
        buffer_.fill(T{});
    }
    
    // Statistical operations
    T mean() const {
        if (samples_count_ == 0) return T{};
        T sum = T{};
        for (size_t i = 0; i < samples_count_; ++i) {
            sum += at(i);
        }
        return sum / static_cast<T>(samples_count_);
    }
    
    T variance() const {
        if (samples_count_ < 2) return T{};
        T m = mean();
        T sum_sq_diff = T{};
        for (size_t i = 0; i < samples_count_; ++i) {
            T diff = at(i) - m;
            sum_sq_diff += diff * diff;
        }
        return sum_sq_diff / static_cast<T>(samples_count_ - 1);
    }
    
    T std_deviation() const {
        return std::sqrt(variance());
    }
    
    T min() const {
        if (samples_count_ == 0) return T{};
        T min_val = at(0);
        for (size_t i = 1; i < samples_count_; ++i) {
            min_val = std::min(min_val, at(i));
        }
        return min_val;
    }
    
    T max() const {
        if (samples_count_ == 0) return T{};
        T max_val = at(0);
        for (size_t i = 1; i < samples_count_; ++i) {
            max_val = std::max(max_val, at(i));
        }
        return max_val;
    }
};

/**
 * @brief Digital filter base class
 */
template<typename T>
class DigitalFilter {
public:
    virtual ~DigitalFilter() = default;
    virtual T process(const T& input) = 0;
    virtual void reset() = 0;
    virtual void setParameters(const std::vector<T>& params) = 0;
};

/**
 * @brief Low-pass Butterworth filter
 */
template<typename T>
class LowPassFilter : public DigitalFilter<T> {
private:
    T cutoff_freq_;
    T sample_rate_;
    T alpha_;
    T prev_output_;
    bool initialized_;
    
public:
    LowPassFilter(T cutoff_freq, T sample_rate) 
        : cutoff_freq_(cutoff_freq), sample_rate_(sample_rate), 
          prev_output_(T{}), initialized_(false) {
        updateCoefficients();
    }
    
    T process(const T& input) override {
        if (!initialized_) {
            prev_output_ = input;
            initialized_ = true;
            return input;
        }
        
        prev_output_ = alpha_ * input + (T{1} - alpha_) * prev_output_;
        return prev_output_;
    }
    
    void reset() override {
        prev_output_ = T{};
        initialized_ = false;
    }
    
    void setParameters(const std::vector<T>& params) override {
        if (params.size() >= 2) {
            cutoff_freq_ = params[0];
            sample_rate_ = params[1];
            updateCoefficients();
        }
    }
    
    void setCutoffFrequency(T freq) {
        cutoff_freq_ = freq;
        updateCoefficients();
    }
    
private:
    void updateCoefficients() {
        T omega = T{2} * static_cast<T>(M_PI) * cutoff_freq_ / sample_rate_;
        alpha_ = omega / (omega + T{1});
    }
};

/**
 * @brief Moving average filter
 */
template<typename T, size_t WindowSize>
class MovingAverageFilter : public DigitalFilter<T> {
private:
    SignalBuffer<T, WindowSize> buffer_;
    T sum_;
    
public:
    MovingAverageFilter() : sum_(T{}) {}
    
    T process(const T& input) override {
        if (buffer_.full()) {
            // Remove oldest sample from sum
            T oldest = buffer_.at(0);
            sum_ -= oldest;
        }
        
        buffer_.push(input);
        sum_ += input;
        
        return sum_ / static_cast<T>(buffer_.size());
    }
    
    void reset() override {
        buffer_.clear();
        sum_ = T{};
    }
    
    void setParameters(const std::vector<T>& params) override {
        // Moving average doesn't have configurable parameters
        // This implementation uses template parameter for window size
    }
};

/**
 * @brief Signal generator interface
 */
template<typename T>
class SignalGenerator {
public:
    virtual ~SignalGenerator() = default;
    virtual T generate() = 0;
    virtual void setFrequency(T frequency) = 0;
    virtual void setAmplitude(T amplitude) = 0;
    virtual void setPhase(T phase) = 0;
    virtual void setSampleRate(T sample_rate) = 0;
    virtual void reset() = 0;
};

/**
 * @brief Sine wave generator
 */
template<typename T>
class SineGenerator : public SignalGenerator<T> {
private:
    T frequency_;
    T amplitude_;
    T phase_;
    T sample_rate_;
    T current_phase_;
    
public:
    SineGenerator(T frequency = T{1}, T amplitude = T{1}, 
                  T phase = T{0}, T sample_rate = T{1000})
        : frequency_(frequency), amplitude_(amplitude), 
          phase_(phase), sample_rate_(sample_rate), current_phase_(phase) {}
    
    T generate() override {
        T value = amplitude_ * std::sin(current_phase_);
        current_phase_ += T{2} * static_cast<T>(M_PI) * frequency_ / sample_rate_;
        
        // Keep phase in range [0, 2π]
        if (current_phase_ >= T{2} * static_cast<T>(M_PI)) {
            current_phase_ -= T{2} * static_cast<T>(M_PI);
        }
        
        return value;
    }
    
    void setFrequency(T frequency) override { frequency_ = frequency; }
    void setAmplitude(T amplitude) override { amplitude_ = amplitude; }
    void setPhase(T phase) override { 
        phase_ = phase; 
        current_phase_ = phase;
    }
    void setSampleRate(T sample_rate) override { sample_rate_ = sample_rate; }
    
    void reset() override {
        current_phase_ = phase_;
    }
};

/**
 * @brief Square wave generator
 */
template<typename T>
class SquareGenerator : public SignalGenerator<T> {
private:
    T frequency_;
    T amplitude_;
    T phase_;
    T sample_rate_;
    T current_phase_;
    
public:
    SquareGenerator(T frequency = T{1}, T amplitude = T{1}, 
                    T phase = T{0}, T sample_rate = T{1000})
        : frequency_(frequency), amplitude_(amplitude), 
          phase_(phase), sample_rate_(sample_rate), current_phase_(phase) {}
    
    T generate() override {
        T normalized_phase = std::fmod(current_phase_, T{2} * static_cast<T>(M_PI));
        T value = (normalized_phase < static_cast<T>(M_PI)) ? amplitude_ : -amplitude_;
        
        current_phase_ += T{2} * static_cast<T>(M_PI) * frequency_ / sample_rate_;
        
        return value;
    }
    
    void setFrequency(T frequency) override { frequency_ = frequency; }
    void setAmplitude(T amplitude) override { amplitude_ = amplitude; }
    void setPhase(T phase) override { 
        phase_ = phase; 
        current_phase_ = phase;
    }
    void setSampleRate(T sample_rate) override { sample_rate_ = sample_rate; }
    
    void reset() override {
        current_phase_ = phase_;
    }
};

/**
 * @brief Fast Fourier Transform utilities
 */
template<typename T>
class FFT {
public:
    using Complex = std::complex<T>;
    
    static std::vector<Complex> fft(const std::vector<T>& input) {
        size_t N = input.size();
        std::vector<Complex> X(N);
        
        // Convert input to complex
        for (size_t i = 0; i < N; ++i) {
            X[i] = Complex(input[i], T{0});
        }
        
        return fft(X);
    }
    
    static std::vector<Complex> fft(const std::vector<Complex>& input) {
        size_t N = input.size();
        
        // Base case
        if (N <= 1) return input;
        
        // Divide
        std::vector<Complex> even, odd;
        for (size_t i = 0; i < N; i += 2) {
            even.push_back(input[i]);
            if (i + 1 < N) {
                odd.push_back(input[i + 1]);
            }
        }
        
        // Conquer
        auto Y_even = fft(even);
        auto Y_odd = fft(odd);
        
        // Combine
        std::vector<Complex> Y(N);
        for (size_t k = 0; k < N / 2; ++k) {
            Complex t = std::polar(T{1}, -T{2} * static_cast<T>(M_PI) * k / N) * Y_odd[k];
            Y[k] = Y_even[k] + t;
            Y[k + N / 2] = Y_even[k] - t;
        }
        
        return Y;
    }
    
    static std::vector<T> magnitude(const std::vector<Complex>& fft_result) {
        std::vector<T> magnitudes;
        magnitudes.reserve(fft_result.size());
        
        for (const auto& complex_val : fft_result) {
            magnitudes.push_back(std::abs(complex_val));
        }
        
        return magnitudes;
    }
    
    static std::vector<T> phase(const std::vector<Complex>& fft_result) {
        std::vector<T> phases;
        phases.reserve(fft_result.size());
        
        for (const auto& complex_val : fft_result) {
            phases.push_back(std::arg(complex_val));
        }
        
        return phases;
    }
};

/**
 * @brief Signal analysis utilities
 */
template<typename T>
class SignalAnalyzer {
public:
    static T rms(const std::vector<T>& signal) {
        if (signal.empty()) return T{};
        
        T sum_squares = T{};
        for (const auto& sample : signal) {
            sum_squares += sample * sample;
        }
        
        return std::sqrt(sum_squares / static_cast<T>(signal.size()));
    }
    
    static T peak(const std::vector<T>& signal) {
        if (signal.empty()) return T{};
        
        return *std::max_element(signal.begin(), signal.end(),
            [](const T& a, const T& b) { return std::abs(a) < std::abs(b); });
    }
    
    static T snr(const std::vector<T>& signal, const std::vector<T>& noise) {
        if (signal.empty() || noise.empty()) return T{};
        
        T signal_power = rms(signal);
        T noise_power = rms(noise);
        
        if (noise_power == T{}) return std::numeric_limits<T>::infinity();
        
        return T{20} * std::log10(signal_power / noise_power);
    }
    
    static T thd(const std::vector<T>& signal, T fundamental_freq, T sample_rate) {
        auto fft_result = FFT<T>::fft(signal);
        auto magnitudes = FFT<T>::magnitude(fft_result);
        
        size_t N = signal.size();
        T freq_resolution = sample_rate / static_cast<T>(N);
        
        // Find fundamental frequency bin
        size_t fundamental_bin = static_cast<size_t>(fundamental_freq / freq_resolution);
        
        if (fundamental_bin >= magnitudes.size()) return T{};
        
        T fundamental_magnitude = magnitudes[fundamental_bin];
        T harmonic_sum = T{};
        
        // Sum harmonics (2nd, 3rd, 4th, etc.)
        for (size_t harmonic = 2; harmonic <= 10; ++harmonic) {
            size_t harmonic_bin = fundamental_bin * harmonic;
            if (harmonic_bin < magnitudes.size()) {
                harmonic_sum += magnitudes[harmonic_bin] * magnitudes[harmonic_bin];
            }
        }
        
        if (fundamental_magnitude == T{}) return T{};
        
        return std::sqrt(harmonic_sum) / fundamental_magnitude;
    }
    
    static std::vector<T> correlate(const std::vector<T>& signal1, 
                                   const std::vector<T>& signal2) {
        size_t N1 = signal1.size();
        size_t N2 = signal2.size();
        size_t N = N1 + N2 - 1;
        
        std::vector<T> correlation(N, T{});
        
        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < N2; ++j) {
                if (i >= j && (i - j) < N1) {
                    correlation[i] += signal1[i - j] * signal2[j];
                }
            }
        }
        
        return correlation;
    }
};

/**
 * @brief Windowing functions for signal processing
 */
template<typename T>
class WindowFunctions {
public:
    static std::vector<T> hann(size_t N) {
        std::vector<T> window(N);
        for (size_t i = 0; i < N; ++i) {
            window[i] = T{0.5} * (T{1} - std::cos(T{2} * static_cast<T>(M_PI) * i / (N - 1)));
        }
        return window;
    }
    
    static std::vector<T> hamming(size_t N) {
        std::vector<T> window(N);
        for (size_t i = 0; i < N; ++i) {
            window[i] = T{0.54} - T{0.46} * std::cos(T{2} * static_cast<T>(M_PI) * i / (N - 1));
        }
        return window;
    }
    
    static std::vector<T> blackman(size_t N) {
        std::vector<T> window(N);
        for (size_t i = 0; i < N; ++i) {
            T arg = T{2} * static_cast<T>(M_PI) * i / (N - 1);
            window[i] = T{0.42} - T{0.5} * std::cos(arg) + T{0.08} * std::cos(T{2} * arg);
        }
        return window;
    }
    
    static std::vector<T> apply_window(const std::vector<T>& signal, 
                                      const std::vector<T>& window) {
        size_t N = std::min(signal.size(), window.size());
        std::vector<T> windowed(N);
        
        for (size_t i = 0; i < N; ++i) {
            windowed[i] = signal[i] * window[i];
        }
        
        return windowed;
    }
};

} // namespace axonvex::types::signals