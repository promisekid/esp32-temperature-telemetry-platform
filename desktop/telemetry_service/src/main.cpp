#include "telemetry_service_app.hpp"

int main(int argc, char **argv) {
    telemetry_platform::telemetry_service::TelemetryServiceApp app;
    return app.run(argc, argv);
}
