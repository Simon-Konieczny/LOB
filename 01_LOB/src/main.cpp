//
// Created by Simon Konieczny on 19/02/2026.
//
#include <thread>

#include "ITCHParser.hpp"
#include "NormalizedMsg.hpp"
#include "OFICalculator.hpp"
#include "ReplayEngine.hpp"
#include "SPSCQueue.hpp"
#include "OrderBook.hpp"
#include "SnapshotWriter.hpp"
#ifdef __APPLE__
typedef size_t rsize_t;
#endif

#include <iostream>
#include <iomanip>
#include <deque>
#include <random>
#include <chrono>

void renderUI(const BookSnapshot& snap, OFICalculator& ofiCalculator) {
    std::cout << "\033[2J\033[1;1H";

    std::cout << "========================================================\n";
    // std::cout << "   LOB MATCHING ENGINE DEMO | Orders Processed: " << totalOrders << "\n";
    // std::cout << "   Last Traded Fill Price  : " << snap.lastTradePrice << "\n";
    std::cout << "   OFI (1s)  : " << ofiCalculator.getOFI_1s() << "\n";
    std::cout << "   OFI (5s)  : " << ofiCalculator.getOFI_5s() << "\n";
    std::cout << "   OFI (30s)  : " << ofiCalculator.getOFI_30s() << "\n";
    std::cout << "========================================================\n";

    std::cout << std::setw(15) << "PRICE" << " | " << std::setw(15) << "QUANTITY" << "\n";
    std::cout << "--------------------------------------------------------\n";

    // Print Asks (Red)
    for (auto it = snap.asks.rbegin(); it != snap.asks.rend(); ++it) {
        std::cout << "\033[31m" << std::setw(15) << it->price
                  << " | " << std::setw(15) << it->volume << "\033[0m\n";
    }

    std::cout << "---------- SPREAD: " << (snap.asks.empty() || snap.bids.empty() ? 0 : snap.asks[0].price - snap.bids[0].price) << " ----------\n";

    // Print Bids (Green)
    for (const auto& bid : snap.bids) {
        std::cout << "\033[32m" << std::setw(15) << bid.price
                  << " | " << std::setw(15) << bid.volume << "\033[0m\n";
    }
}

class QueueProducerAdapter {
public:
    QueueProducerAdapter(SPSCQueue<NormalizedMsg>& queue) : queue_(queue) {}

    inline void onMessage(const NormalizedMsg& msg) {
        while (!queue_.push(msg)) {
            #if defined(__aarch64__) || defined(__arm__)
                        __asm__ volatile("yield" ::: "memory");
            #else
                        __asm__ volatile("pause" ::: "memory");
            #endif
        }
    }
private:
    SPSCQueue<NormalizedMsg>& queue_;
};

int main() {
    SPSCQueue<NormalizedMsg> orderMessageQueue(65536);
    SPSCQueue<ITradeObserver::TradeRecord> tradeRecordQueue(65536);
    SPSCQueue<SnapshotRow> snapshotQueue(65536);

    std::atomic<bool> producerDone(false);
    std::atomic<bool> engineDone(false);

    OFICalculator ofiCalculator;
    OrderBook book(tradeRecordQueue);

    SnapshotWriter writer(snapshotQueue, producerDone, book, ofiCalculator, 100'000'000);

    book.addObserver(&ofiCalculator);
    book.addObserver(&writer);

    QueueProducerAdapter adapter(orderMessageQueue);
    ITCHParser<QueueProducerAdapter> parser(adapter);

    std::string dataFile = "../../../03_data/12302019.NASDAQ_ITCH50";
    std::string targetTicker = "AAPL    ";
    double speedMultiplier = 3000.0;

    std::thread parserThread([&]()
    {
        parser.parse(dataFile, targetTicker);
        producerDone.store(true, std::memory_order_release);
        std::cout << "parserThread thread done.\n";
    });

    std::thread writerThread([&]() {
        writer.runSnapshotCapture("../../../03_data/lob_snapshots.csv");
    });

    ReplayEngine engine(orderMessageQueue, producerDone, tradeRecordQueue, book);

    std::thread observerThread([&]()
    {
        while (!engineDone.load(std::memory_order_acquire))
        {
            auto snap = engine.getOrderBook().getSnapshot(10);
            renderUI(snap, ofiCalculator);

            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        }
    });

    engine.runReplay(speedMultiplier);

    engineDone.store(true, std::memory_order_release);

    parserThread.join();
    writerThread.join();
    observerThread.join();

    auto snap = engine.getOrderBook().getSnapshot(10);
    renderUI(snap, ofiCalculator);

    return 0;
}
