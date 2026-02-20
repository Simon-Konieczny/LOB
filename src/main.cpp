//
// Created by Simon Konieczny on 19/02/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <iostream>

#include "OrderBook.hpp"
void printBook(const BookSnapshot& snapshot) {
    std::cout << "\n--- ORDER BOOK SNAPSHOT ---\n";
    std::cout << "  ASKS (Sellers)  \n";
    for (auto it = snapshot.asks.rbegin(); it != snapshot.asks.rend(); ++it) {
        std::cout << "  " << it->price << " | " << it->volume << "\n";
    }
    std::cout << "---------------------------\n";
    for (const auto& bid : snapshot.bids) {
        std::cout << "  " << bid.price << " | " << bid.volume << "\n";
    }
    std::cout << "  BIDS (Buyers)   \n\n";
}

int main() {
    OrderBook book;

    book.addOrder(1, 15000, 10, Side::Sell); // Sell at 1.50
    book.addOrder(2, 15100, 5, Side::Sell);  // Sell at 1.51
    book.addOrder(3, 14900, 10, Side::Buy);  // Buy at 1.49

    std::cout << "Matching a Buy order at 1.50..." << std::endl;
    book.addOrder(4, 15000, 10, Side::Buy);

    auto snap = book.getSnapshot(5);
    printBook(snap);

    return 0;
}
