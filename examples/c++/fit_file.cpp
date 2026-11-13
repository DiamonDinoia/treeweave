#include <treeweave/treeweave.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>

int main() {
    // `options::filename` saves the fit to a plain-text file. The next fit
    // with the same arguments loads the file and does not call f.
    const auto path = (std::filesystem::temp_directory_path() / "treeweave_fit_file_example.txt").string();
    std::filesystem::remove(path);

    int  calls = 0;
    auto f     = [&calls](double x) {
        ++calls;
        return std::exp(x) * std::sin(5.0 * x);
    };
    treeweave::options opts;
    opts.filename = path;

    // First fit: the file does not exist, so treeweave fits f and writes the file.
    auto fitted = treeweave::fit(f, 0.0, 2.0, /*tol=*/1e-10, opts);
    std::cout << "fit:  " << calls << " calls to f, file " << path << "\n";

    // Second fit, same arguments: treeweave loads the file.
    calls       = 0;
    auto loaded = treeweave::fit(f, 0.0, 2.0, /*tol=*/1e-10, opts);
    std::cout << "load: " << calls << " calls to f\n";

    // The file keeps the digits that tol needs, so the loaded fit differs
    // from the fitted one by at most tol * max|f| / 10.
    double max_f = 0.0, max_diff = 0.0;
    for (int i = 0; i <= 100; ++i) {
        const double x = 2.0 * i / 100.0;
        max_f          = std::max(max_f, std::abs(fitted(x)));
        max_diff       = std::max(max_diff, std::abs(fitted(x) - loaded(x)));
    }
    std::cout << "max |loaded - fitted| / max|f|: " << max_diff / max_f << " (tol 1e-10)\n";

    std::filesystem::remove(path);
    return calls == 0 && max_diff <= 1e-10 * max_f ? 0 : 1;
}
