#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <iomanip>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

using uint16 = uint16_t;

inline uint16 rotate_lattice(uint16 x, int shift) {
    return (x << (shift % 16)) | (x >> (16 - (shift % 16)));
}

inline float interact(uint16 a, uint16 b) {
    int count = __builtin_popcount(~(a ^ b) & 0xFFFF);
    return (count - 8.0f) / 4.0f; 
}

struct HexProbeHelix {
    size_t dim;
    uint16 *layers[4];
    float bias = 0.0f;
    std::mt19937 gen;

    HexProbeHelix(size_t d) : dim(d), gen(42) {
        const char* names[] = {"sw_a1.bin", "sw_a2.bin", "sw_b1.bin", "sw_b2.bin"};
        for(int i = 0; i < 4; ++i) {
            int fd = open(names[i], O_RDWR | O_CREAT, 0666);
            ftruncate(fd, dim * 2);
            layers[i] = (uint16*)mmap(NULL, dim * 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            for(size_t j=0; j<dim; ++j) if(layers[i][j] == 0) layers[i][j] = (uint16)gen();
        }
    }

    float forward(const std::vector<uint16>& x, int phase) {
        float acc = 0;
        for (size_t i = 0; i < dim; ++i) {
            uint16 rx = rotate_lattice(x[i], phase);
            float v0 = interact(layers[0][i], rx);
            float v1 = interact(layers[1][i], rx);
            float op = interact(layers[2][i], rx) * interact(layers[3][i], rx);
            acc += (v0 - v1) * op; 
        }
        return 1.0f / (1.0f + std::exp(-(acc / (dim * 0.1f) + bias)));
    }

    void update(const std::vector<uint16>& x, float target, int phase) {
        float r = forward(x, phase);
        float err = target - r;
        bias += err * 0.15f;
        std::uniform_real_distribution<float> p(0, 1);
        for (size_t i = 0; i < dim; ++i) {
            if (p(gen) < 0.10f) {
                uint16 rx = rotate_lattice(x[i], phase);
                int idx = (err > 0) ? (i % 2) : (2 + (i % 2));
                layers[idx][i] ^= (rx & 0x0F0F); 
            }
        }
    }
};

int main() {
    size_t dim = 4096; HexProbeHelix helix(dim);
    const int N = 1000;
    std::vector<std::vector<uint16>> X(N);
    std::vector<float> y(N);
    std::mt19937 rng(42);
    
    for (int i = 0; i < N; ++i) {
        float x1 = (rng() % 100 - 50) / 50.0f;
        float x2 = (rng() % 100 - 50) / 50.0f;
        y[i] = (x1 * x2 > 0) ? 1.0f : 0.0f; 
        X[i].resize(dim);
        for (size_t j = 0; j < dim; ++j) {
            X[i][j] = (uint16)(0x8000 + (x1 * 10000) + (x2 * 10000 * (j % 2 ? 1 : -1)));
        }
    }

    std::cout << "EP | Acc   | Health | Phase | Bias    | Conf(Φ) | Entr(Ω)\n";
    std::cout << "---|-------|--------|-------|---------|---------|---------\n";
    for (int e = 0; e < 100; ++e) {
        int hits = 0; float h_sum = 0, c_sum = 0, entr = 0;
        int phase = (e % 16); 

        for (int i = 0; i < N; ++i) {
            float r = helix.forward(X[i], phase);
            if ((r > 0.5f) == (y[i] > 0.5f)) hits++;
            h_sum += (y[i] > 0.5f ? r : (1.0f - r));
            c_sum += std::abs(r - 0.5f) * 2.0f;
            entr += -r * std::log2(r + 1e-9) - (1-r) * std::log2(1-r + 1e-9);
            helix.update(X[i], y[i], phase);
        }

        std::cout << std::setw(2) << e << " | " << std::fixed << std::setprecision(3) << (float)hits/N 
                  << " | " << std::setprecision(2) << (h_sum/N)*100.0f << "%"
                  << " | " << std::setw(5) << phase << " | "
                  << std::setw(7) << std::setprecision(4) << helix.bias << " | "
                  << std::setprecision(3) << (c_sum/N) << " | "
                  << std::setprecision(3) << (entr/N) << std::endl;
        
        if (hits > N * 0.999) break;
    }
    return 0;
}
