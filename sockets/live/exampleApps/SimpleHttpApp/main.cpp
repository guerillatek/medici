#include "medici/application/AppRunContextManager.hpp"
#include "medici/cryptoUtils/OpenSSLUtils.hpp"
#include "medici/http/writeBufferToTempFile.hpp"
#include "medici/sockets/live/HttpServerHandler.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"
#include "medici/sockets/live/LiveSocketFactory.hpp"
#include "medici/sockets/live/testCredentials/sslTestCerts.hpp"
#include "medici/time.hpp"

std::string getUserHomeDirectory() {
  const char *homeDir = std::getenv("HOME");
  if (homeDir) {
    return std::string(homeDir);
  }

  // Fallback for systems where HOME might not be set
  const char *userProfile = std::getenv("USERPROFILE");
  if (userProfile) {
    return std::string(userProfile);
  }

  // Last resort - use current directory
  return std::filesystem::current_path().string();
}







int main() {
  using namespace medici;

  Expected sslInitialization = crypto_utils::OpenSSLUtils::initSSL();

  std::string serverCertFilePath = http::writeBufferToTempFile(
      std::string_view{serverCertificate}, "serverCert", ".pem");

  std::string serverKeyFilePath = http::writeBufferToTempFile(
      std::string_view{serverKey}, "serverKey", ".key");

  using AppRunContextManagerT = medici::application::AppRunContextManager<
      medici::sockets::live::LiveSocketFactory, medici::SystemClockNow,
      medici::sockets::live::IPEndpointPollManager>;
  AppRunContextManagerT runContextManager;
  medici::application::ContextThreadConfigList configs;
  // Populate configs as needed
  configs.push_back(
      std::make_shared<medici::application::IPContextThreadConfig>(
          "SimpleHttpApp", 1, 100, serverCertFilePath, serverKeyFilePath));
  auto configResult = runContextManager.configureContexts(configs);
  if (!configResult) {
    std::cerr << "Failed to configure contexts: " << configResult.error()
              << "\n";
    return 1;
  }

  using IPAppRunContextT = AppRunContextManagerT::IPAppRunContextT;

  auto &runContext =
      runContextManager.getAppRunContext<IPAppRunContextT>("SimpleHttpApp");

  using HttpServerHandlerT =
      sockets::HttpServerHandler<sockets::live::TcpIpLiveEndpoint,
                                 IPAppRunContextT>;

  HttpServerHandlerT httpServerHandler{
      runContext, getUserHomeDirectory(),
      sockets::HttpEndpointConfig{"SimpleHttpServer", "127.0.0.1", 8443,
                                  "/files/"}};

  runContext.getEventQueue().postAction([&httpServerHandler]() {
    if (auto result = httpServerHandler.start(); !result) {
      std::cerr << "Failed to start HTTP server handler: " << result.error()
                << "\n";
    } else {
      std::cout << "HTTP server handler started successfully\n";
    }
    return Expected{};
  });

  runContextManager.startAllThreads();

  return 0;
}
