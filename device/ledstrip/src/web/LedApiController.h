#ifndef LED_API_CONTROLLER_H
#define LED_API_CONTROLLER_H

#include <HttpDispatcher.h>

namespace LedApiController {

// Register all LED/shader API routes on the dispatcher
void registerRoutes(HttpDispatcher& dispatcher);

}  // namespace LedApiController

#endif  // LED_API_CONTROLLER_H