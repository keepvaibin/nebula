#pragma once

namespace Ui {
    class NonCopyable {
    protected:
        inline NonCopyable() {
        }
        inline ~NonCopyable() {
        }

    private:
        NonCopyable(const NonCopyable&);
        const NonCopyable& operator=(const NonCopyable&);
    };

    namespace {
        template < typename T >
        inline T Min(T a, T b) {
            return (a > b) ? b : a;
        }

        template < typename T >
        inline T Max(T a, T b) {
            return (a < b) ? b : a;
        }
    };  // namespace
}  // namespace Ui
