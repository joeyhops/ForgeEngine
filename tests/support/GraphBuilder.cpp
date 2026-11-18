#include "support/GraphBuilder.h"

namespace forge::test {

int16_t GraphBuilder::clip(std::string actionKey, bool loop) {
  auto d = std::make_unique<ClipNode::Definition>();
  d->actionKey = std::move(actionKey);
  d->ownerName = "test";
  d->loop = loop;
  return add(std::move(d));
}

int16_t GraphBuilder::blend1D(std::string param, std::vector<float> thresholds,
                              std::vector<int16_t> children) {
  auto d = std::make_unique<Blend1DNode::Definition>();
  d->paramName = std::move(param);
  d->thresholds = std::move(thresholds); 
  d->childNodeIndices = std::move(children);
  return add(std::move(d));
}

int16_t GraphBuilder::blend2D(std::string xParam,
                              std::string zParam,
                              std::vector<Blend2DNode::ClipEntry> clips) {
  auto d = std::make_unique<Blend2DNode::Definition>();
  d->xParamName = std::move(xParam);
  d->zParamName = std::move(zParam);
  d->clips = std::move(clips);
  return add(std::move(d));
}

int16_t GraphBuilder::selector(std::string param,
                               std::vector<ClipSelectorNode::Entry> entries) {
  auto d = std::make_unique<ClipSelectorNode::Definition>();
  d->paramName = std::move(param);
  d->ownerName = "test";
  d->entries = std::move(entries);
  return add(std::move(d));
}

int16_t GraphBuilder::stateMachine(std::string initialState,
                                   std::vector<StateMachineNode::StateDef> states,
                                   std::vector<StateMachineNode::TransitionDef> transitions) {
  auto d = std::make_unique<StateMachineNode::Definition>();
  d->initialState = std::move(initialState);
  d->states = std::move(states);
  d->transitions = std::move(transitions);
  return add(std::move(d));
}

}
