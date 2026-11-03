#pragma once

#include <algorithm>
#include <utility>

// The game's sort: equal elements end up in the same order on every host, which collision (Binder) depends on.
namespace MR {
    template < typename TCompare, typename TIt >
    inline void selectionSort(TIt first, TIt last, TCompare compare) {
        if (first != last) {
            TIt j = last;
            for (j--; first != j; first++) {
                TIt i = std::min_element< TIt, TCompare& >(first, last, compare);

                if (i != first) {
                    std::swap(*i, *first);
                }
            }
        }
    }

    template < typename TCompare, typename TIt >
    void sort132(TIt a1, TIt a2, TIt a3, TCompare compare) {
        bool b1 = !compare(*a3, *a1);
        bool b2 = !compare(*a2, *a3);

        if (!b1 || !b2) {
            if (!b1 && !b2) {
                std::swap(*a1, *a2);
                return;
            }

            if (compare(*a2, *a1)) {
                std::swap(*a1, *a2);
            }

            if (b1) {
                std::swap(*a2, *a3);
                return;
            }

            std::swap(*a1, *a3);
        }
    }

    template < typename TIt, typename TCompare >
    void sort(TIt first, TIt last, TCompare compare) {
        static const int SHUFFLE_MIN = -4;
        static const int SHUFFLE_MAX = 5;
        static const int SELECTION_SORT_MIN = 20;
        // The pivots sit at a quarter and three quarters of the range, whatever the iterator's size: sizeof(TIt)
        // here would put the second pivot past the end of short ranges.
        static const long PIVOT_PARTS = 4;

        while (true) {
            long len = static_cast< long >(last - first);
            if (len <= 1) {
                return;
            }

            if (len <= SELECTION_SORT_MIN) {
                selectionSort< TCompare&, TIt >(first, last, compare);
                return;
            }

            static int shuffle = -4;
            TIt m = first + (len / PIVOT_PARTS + shuffle % SHUFFLE_MAX);

            if (++shuffle >= SHUFFLE_MAX) {
                shuffle = SHUFFLE_MIN;
            }

            TIt i1 = first + (len * (PIVOT_PARTS - 1) / PIVOT_PARTS + shuffle % SHUFFLE_MAX);

            if (++shuffle >= SHUFFLE_MAX) {
                shuffle = SHUFFLE_MIN;
            }

            TIt j = last - 1;
            sort132< TCompare&, TIt >(m, i1, j, compare);

            m = first;
            i1 = j;
            while (compare(*m, *j)) {
                m++;
            }

            do {
                if (m == --i1) {
                    break;
                }
            } while (!compare(*i1, *j));

            if (m < i1) {
                std::swap(*m, *i1);
                m++;

                while (true) {
                    while (compare(*m, *j)) {
                        m++;
                    }

                    while (!compare(*(--i1), *j)) {
                        ;
                    }

                    if (m < i1) {
                        std::swap(*m, *i1);
                        m++;
                    } else {
                        break;
                    }
                }
            }

            if (m == first) {
                std::swap(*m, *j);
                m++;

                i1 = last;
                if (!compare(*first, *(--i1))) {
                    while (m != last && !compare(*first, *m)) {
                        m++;
                    }

                    if (m < i1) {
                        std::swap(*m, *i1);
                    }
                }

                if (m < i1) {
                    while (true) {
                        while (!compare(*first, *m)) {
                            m++;
                        }

                        while (compare(*first, *(--i1))) {
                            ;
                        }

                        if (m < i1) {
                            std::swap(*m, *i1);
                            m++;
                        } else {
                            break;
                        }
                    }
                }

                first = m;
            } else if (m - first < last - m) {
                sort< TIt, TCompare& >(first, m, compare);
                first = m;
            } else {
                sort< TIt, TCompare& >(m, last, compare);
                last = m;
            }
        }
    }

}  // namespace MR
