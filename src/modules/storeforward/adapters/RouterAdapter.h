#pragma once

#include "MeshService.h"
#include "Router.h"
#include "interfaces/INetworkRouter.h"

/**
 * Adapter that implements INetworkRouter using the existing Router and MeshService
 */
class RouterAdapter : public INetworkRouter
{
  public:
    RouterAdapter(Router &router, MeshService &service) : router(router), service(service) {}

    meshtastic_MeshPacket *allocForSending() override { return router.allocForSending(); }

    void sendToMesh(meshtastic_MeshPacket *packet) override { service.sendToMesh(packet); }

    bool cancelSending(NodeNum from, PacketId id) override { return router.cancelSending(from, id); }

  private:
    Router &router;
    MeshService &service;
};
