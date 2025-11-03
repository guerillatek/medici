#include "medici/event_queue/EventQueue.hpp"
#include "medici/shm_endpoints/SharedMemEndpointFactory.hpp"
#include "medici/shm_endpoints/shmUtils.hpp"

#include <algorithm>
#include <format>
#include <list>
#include <memory>
#include <random>
#include <set>
#include <vector>

// For fork() and process management
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace medici::tests {

enum class Markets {
  BINANCE,
  COINBASE,
  KRAKEN,
  BYBIT,
  MEXC,
  OKX,
  HUOBI,
  GATEIO,
  BITFINEX,
  BITSTAMP
};

std::string to_string(Markets m) {
  switch (m) {
  case Markets::BINANCE:
    return "BINANCE";
  case Markets::COINBASE:
    return "COINBASE";
  case Markets::KRAKEN:
    return "KRAKEN";
  case Markets::BYBIT:
    return "BYBIT";
  case Markets::MEXC:
    return "MEXC";
  case Markets::OKX:
    return "OKX";
  case Markets::HUOBI:
    return "HUOBI";
  case Markets::GATEIO:
    return "GATEIO";
  case Markets::BITFINEX:
    return "BITFINEX";
  case Markets::BITSTAMP:
    return "BITSTAMP";
  default:
    return "UNKNOWN";
  };
}

struct TradeSubscription {
  char securityId[64];
  Markets market;
};

struct QuoteSubscription {
  char securityId[64];
  Markets market;
};

struct Trade {
  char securityId[64];
  int tradeId;
  int64_t price;
  int quantity;
};

struct Quote {
  char securityId[64];
  int64_t bidPrice;
  int64_t askPrice;
  int bidSize;
  int askSize;
};

using MarketDataPayload = std::variant<Trade, Quote>;
using SubscriptionPayload = std::variant<TradeSubscription, QuoteSubscription>;

struct TestRunContext {
  using EventQueueT =
      event_queue::EventQueue<TestRunContext, SystemClockNow, 1024, 1024>;

  TestRunContext(const std::string &sessionName, const SystemClockNow &clock)
      : eventQueue{sessionName, *this, clock, 1, std::chrono::microseconds{10}},
        sharedMemEndpointFactory{eventQueue} {}

  ExpectedEventsCount pollAndDispatchEndpointsEvents() {
    return sharedMemEndpointFactory.pollAndDispatchEndpointsEvents();
  };

  EventQueueT eventQueue;
  shm_endpoints::SharedMemEndpointFactory<EventQueueT> sharedMemEndpointFactory;
  using ProducerServerEndpointPtr =
      typename shm_endpoints::SharedMemEndpointFactory<
          EventQueueT>::template ProducerServerEndpointPtr<Trade, Quote>;

  auto start() { return eventQueue.start(); }
  void stop() { eventQueue.stop(); }
};

void runServerConsumerFunction(std::uint32_t producers,
                               std::function<void()> queueReadyCallback,
                               std::uint32_t expectedMessages) {
  SystemClockNow clock;
  TestRunContext serverRunContext{"ServerThread", clock};
  std::uint32_t receivedMessages = 0;
  // Create a server consumer endpoint to receive subscription messages from
  // clients
  auto expectedServerConsumer =
      serverRunContext.sharedMemEndpointFactory.createServerConsumerEndpoint<
          TradeSubscription, QuoteSubscription>(
          "SubscriptionQueue", producers, 1024,
          [&](const SubscriptionPayload &payload) -> Expected {
            std::visit(
                [&](const auto &msg) -> Expected {
                  using T = std::decay_t<decltype(msg)>;
                  if constexpr (std::is_same_v<T, TradeSubscription>) {
                    std::cout << "Received TradeSubscription: SecurityId="
                              << msg.securityId
                              << ", Market=" << to_string(msg.market)
                              << std::endl;
                    receivedMessages++;
                  } else if constexpr (std::is_same_v<T, QuoteSubscription>) {
                    std::cout << "Received QuoteSubscription: SecurityId="
                              << msg.securityId
                              << ", Market=" << to_string(msg.market)
                              << std::endl;
                    receivedMessages++;
                  }
                  if (receivedMessages >= expectedMessages) {
                    serverRunContext.stop();
                  }
                  return {};
                },
                payload);
            return {};
          });

  if (!expectedServerConsumer) {
    std::cerr << "Failed to create server consumer endpoint: "
              << expectedServerConsumer.error() << std::endl;
    return;
  }

  auto serverConsumer = std::move(expectedServerConsumer.value());

  std::cout << "Server endpoints created, starting event queue." << std::endl;
  queueReadyCallback();
  auto result = serverRunContext.start();
  if (!result) {
    std::cerr << "Server event queue terminated with failure: "
              << result.error() << std::endl;
  }
}

void runClientProducerFunction(const std::string &producerId) {
  SystemClockNow clock;
  TestRunContext clientProducerRunContext{"clientProducerThread", clock};

  // Create a client producer endpoint to send subscription messages
  auto expectedClientProducer =
      clientProducerRunContext.sharedMemEndpointFactory
          .createClientProducerEndpoint<TradeSubscription, QuoteSubscription>(
              "SubscriptionQueue", producerId);

  if (!expectedClientProducer) {
    std::cerr << "Failed to create producer client endpoint: "
              << expectedClientProducer.error() << std::endl;
    return;
  }

  auto clientProducer = std::move(expectedClientProducer.value());

  // Send subscription messages
  std::vector<std::string> securities = {"BTC",  "ETH",  "SOL",  "XPR",
                                         "OPEN", "USDC", "USDT", "DOGE",
                                         "ADA",  "MATIC"};
  std::vector<Markets> markets = {
      Markets::BINANCE,  Markets::COINBASE, Markets::KRAKEN, Markets::BYBIT,
      Markets::MEXC,     Markets::OKX,      Markets::HUOBI,  Markets::GATEIO,
      Markets::BITFINEX, Markets::BITSTAMP};

  std::uint32_t messagesSent = 0;

  clientProducerRunContext.eventQueue.postAsyncAction(
      [&]() -> event_queue::AsyncExpected {
        // Send trade subscriptions
        for (const auto &security : securities) {
          for (const auto &market : markets) {
            TradeSubscription tradeSubscription;
            std::strncpy(tradeSubscription.securityId, security.c_str(),
                         sizeof(tradeSubscription.securityId) - 1);
            tradeSubscription
                .securityId[sizeof(tradeSubscription.securityId) - 1] = '\0';
            tradeSubscription.market = market;

            auto pushResult = clientProducer->pushMessage(tradeSubscription);
            if (!pushResult) {
              std::cerr << "Failed to push trade subscription for " << security
                        << ": " << pushResult.error() << std::endl;
              return std::unexpected(pushResult.error());
            }

            ++messagesSent;
            std::cout << "Sent TradeSubscription: SecurityId="
                      << tradeSubscription.securityId
                      << ", Market=" << to_string(market) << std::endl;

            // Send quote subscription for the same security/market
            QuoteSubscription quoteSubscription;
            std::strncpy(quoteSubscription.securityId, security.c_str(),
                         sizeof(quoteSubscription.securityId) - 1);
            quoteSubscription
                .securityId[sizeof(quoteSubscription.securityId) - 1] = '\0';
            quoteSubscription.market = market;

            auto quotePushResult =
                clientProducer->pushMessage(quoteSubscription);
            if (!quotePushResult) {
              std::cerr << "Failed to push quote subscription for " << security
                        << ": " << quotePushResult.error() << std::endl;
              return std::unexpected(quotePushResult.error());
            }

            ++messagesSent;
            std::cout << "Sent QuoteSubscription: SecurityId="
                      << quoteSubscription.securityId
                      << ", Market=" << to_string(market) << std::endl;
          }
        }

        std::cout << "Producer client finished sending " << messagesSent
                  << " subscription messages." << std::endl;

        // shutdown When all messages sent and no backpressure
        clientProducerRunContext.eventQueue.postPrecisionTimedAction(
            clock() + std::chrono::seconds{1}, [&]() mutable {
              if ((messagesSent >= 200) &&
                  (clientProducer->getBackPressureQueue().empty())) {
                clientProducerRunContext.stop();
              }

              return medici::Expected{};
            });

        return true;
      });

  std::cout << "Producer client endpoint created, starting event queue."
            << std::endl;
  auto result = clientProducerRunContext.start();
  if (!result) {
    std::cerr << "Producer client event queue terminated with failure: "
              << result.error() << std::endl;
  }
}

void runClientConsumerFunction(const std::string &consumerId) {
  SystemClockNow clock;
  TestRunContext consumerRunContext{"ConsumerThread", clock};
  std::uint32_t messagesReceived = 0;

  auto consumerEndpoint =
      consumerRunContext.sharedMemEndpointFactory
          .createClientConsumerEndpoint<Trade, Quote>(
              "TestSharedMem", consumerId,
              [&messagesReceived, &consumerRunContext](
                  const MarketDataPayload &payload) -> Expected {
                std::visit(
                    [&messagesReceived](const auto &msg) {
                      using T = std::decay_t<decltype(msg)>;
                      if constexpr (std::is_same_v<T, Trade>) {
                        ++messagesReceived;
                        std::cout << "Received Trade: "
                                  << "SecurityId=" << msg.securityId
                                  << ", TradeId=" << msg.tradeId
                                  << ", Price=" << msg.price
                                  << ", Quantity=" << msg.quantity << std::endl;
                      } else if constexpr (std::is_same_v<T, Quote>) {
                        ++messagesReceived;
                        std::cout << "Received Quote: "
                                  << "SecurityId=" << msg.securityId
                                  << ", BidPrice=" << msg.bidPrice
                                  << ", AskPrice=" << msg.askPrice
                                  << ", BidSize=" << msg.bidSize
                                  << ", AskSize=" << msg.askSize << std::endl;
                      }
                    },
                    payload);
                if (messagesReceived >= 200) {
                  consumerRunContext.stop();
                }
                return {};
              });

  if (!consumerEndpoint) {
    std::cerr << "Failed to create consumer endpoint: "
              << consumerEndpoint.error() << std::endl;
    return;
  }
  std::cout << "Consumer endpoint created, starting event queue." << std::endl;
  auto result = consumerRunContext.start();
  if (!result) {
    std::cerr << "Event queue terminated with failure: " << result.error()
              << std::endl;
  }
}

void runServerProducerFunction(std::uint32_t numConsumers,
                               std::function<void()> queueReadyCallback) {
  // Push messages from producer
  SystemClockNow clock;
  TestRunContext producerRunContext{"ProducerThread", clock};

  auto expectedProducer =
      producerRunContext.sharedMemEndpointFactory.createServerProducerEndpoint<
          medici::tests::Trade, medici::tests::Quote>("TestSharedMem",
                                                      numConsumers, 10);
  if (!expectedProducer) {
    return;
  }

  TestRunContext::ProducerServerEndpointPtr producer =
      std::move(expectedProducer.value());

  std::jthread queueReady(queueReadyCallback);

  producerRunContext.eventQueue.postAsyncAction(
      [&producerRunContext, &producer,
       numConsumers]() -> event_queue::AsyncExpected {
        if (producer->getActiveConsumerChannelCount() < numConsumers) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          return false;
        }
        auto result = producerRunContext.eventQueue.postAction(
            [&producerRunContext, &producer]() -> medici::Expected {
              for (int i = 0; i < 100; ++i) {
                medici::tests::Trade tradeMessage;
                std::snprintf(tradeMessage.securityId,
                              sizeof(tradeMessage.securityId), "SEC%03d",
                              i % 10);
                tradeMessage.tradeId = i;
                tradeMessage.price = 10000 + i * 10;
                tradeMessage.quantity = 100 + i;
                auto pushResult = producer->pushMessage(
                    tradeMessage,
                    [&producer](const auto &message) -> medici::Expected {
                      std::cout << "Backpressure on Trade message: SecurityId="
                                << message.securityId
                                << ", TradeId=" << message.tradeId << std::endl;
                      producer->applyDefaultBackPressureHandler(message);
                      return {};
                    });
                if (!pushResult) {
                  return std::unexpected("Failed to push trade message: " +
                                         pushResult.error());
                }

                medici::tests::Quote quoteMessage;
                std::snprintf(quoteMessage.securityId,
                              sizeof(quoteMessage.securityId), "SEC%03d",
                              i % 10);
                quoteMessage.bidPrice = 9990 + i * 10;
                quoteMessage.askPrice = 10010 + i * 10;
                quoteMessage.bidSize = 500 + i;
                quoteMessage.askSize = 600 + i;
                auto quotePushResult = producer->pushMessage(
                    quoteMessage,
                    [&producer](const auto &message) -> medici::Expected {
                      std::cout << "Backpressure on Quote message: SecurityId="
                                << message.securityId << std::endl;
                      producer->applyDefaultBackPressureHandler(message);
                      return {};
                    });

                if (!quotePushResult) {
                  return std::unexpected("Failed to push quote message: " +
                                         quotePushResult.error());
                }
              }
              std::cout << "Producer finished pushing messages." << std::endl;
              std::cout << "Backpressure channels sizes: ";
              auto &backPressureChannels = producer->getBackPressureChannels();
              for (std::uint32_t channelIdx = 0;
                   channelIdx < producer->getActiveConsumerChannelCount();
                   ++channelIdx) {
                const auto &queue = backPressureChannels[channelIdx];
                std::cout << "Channel " << channelIdx + 1 << ": "
                          << queue.size() << " ";
              }
              std::cout << std::endl;
              return {};
            });
        if (!result) {
          return std::unexpected(result.error());
        }
        return true;
      });

  producerRunContext.eventQueue.postPrecisionTimedAction(
      clock() + std::chrono::seconds{1},
      [&producerRunContext, &producer]() mutable {
        auto &backPressureChannels = producer->getBackPressureChannels();
        for (std::uint32_t channelIdx = 0;
             channelIdx < producer->getActiveConsumerChannelCount();
             ++channelIdx) {
          const auto &queue = backPressureChannels[channelIdx];
          if (!queue.empty()) {
            return medici::Expected{};
          }
        }
        producerRunContext.stop();
        return medici::Expected{};
      });
  auto result = producerRunContext.start(); // Start producer event queue
  queueReady.join();
}

} // namespace medici::tests