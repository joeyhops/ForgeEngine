#include <forge/EventBus.h>
#include <memory>
#include <unordered_map>

namespace forge {

std::unordered_map<std::type_index, std::unique_ptr<HandlerListBase>> EventBus::s_handlers;

}
