# plf::stack
A data container replicating std::stack functionality but with better performance than standard library containers in a stack context.
C++98/03/11/14/etc-compatible. Full documentation/function descriptions here: https://plflib.org/stack.htm

plf::stack is faster than all std:: containers in the context of a stack. It has the following averaged performance characteristics versus std::stack (assuming underlying container is the default, std::deque):

    82% faster for 1 byte types
    83% faster for 4 byte types
    80% faster for 8 byte types
    82% faster for 40 byte types
    724% faster for 490 byte types (note: libstdc++ basically turns a deque into a linked list at this point due to their 512-byte limit on block capacities)

Averaged across total numbers of stored elements ranging between 10 and 1000000. The benchmark in question is total time taken to construct, push all elements, read and pop all elements, then destruct.

Benchmarks are here: https://plflib.org/stack.htm#benchmarks
