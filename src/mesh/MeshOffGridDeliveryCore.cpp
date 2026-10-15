#include "MeshOffGridDeliveryCore.h"

namespace meshoffgrid
{

bool DeliveryStateMachine::canTransition(DeliveryState from, DeliveryState to)
{
    if (from == to)
        return false;

    if (to == DeliveryState::Expired)
        return !isTerminal(from);

    switch (from) {
    case DeliveryState::Created:
        return to == DeliveryState::Queued || to == DeliveryState::Failed;
    case DeliveryState::Queued:
        return to == DeliveryState::Routing || to == DeliveryState::Failed;
    case DeliveryState::Routing:
        return to == DeliveryState::Sending || to == DeliveryState::Fallback || to == DeliveryState::RetryWait ||
               to == DeliveryState::Failed;
    case DeliveryState::Sending:
        return to == DeliveryState::TransportAck || to == DeliveryState::RemoteAck || to == DeliveryState::Fallback ||
               to == DeliveryState::RetryWait || to == DeliveryState::Failed;
    case DeliveryState::TransportAck:
        return to == DeliveryState::RemoteAck || to == DeliveryState::Fallback || to == DeliveryState::RetryWait ||
               to == DeliveryState::Failed;
    case DeliveryState::RemoteAck:
        return to == DeliveryState::Delivered || to == DeliveryState::Failed;
    case DeliveryState::RetryWait:
    case DeliveryState::Fallback:
        return to == DeliveryState::Routing || to == DeliveryState::Failed;
    case DeliveryState::Delivered:
    case DeliveryState::Expired:
    case DeliveryState::Failed:
        return false;
    }

    return false;
}

bool DeliveryStateMachine::isTerminal(DeliveryState state)
{
    return state == DeliveryState::Delivered || state == DeliveryState::Expired || state == DeliveryState::Failed;
}

bool DeliveryStateMachine::isExpired(const MessageEnvelope &message, uint32_t nowEpochSeconds)
{
    return message.expiresAt != 0 && nowEpochSeconds >= message.expiresAt;
}

} // namespace meshoffgrid
