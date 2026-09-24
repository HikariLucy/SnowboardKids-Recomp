#pragma once

#include "service.hpp"

// Adapters that need the full application (VI event thread, librecomp
// overlays/RSP DMEM, RT64 renderer). Compiled only into SnowboardKidsRecompiled.
namespace sbk::savestate {

class ViDomain final : public Domain {
public:
    DomainId id() const override { return DomainId::Vi; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
};

class OverlayDomain final : public Domain {
public:
    DomainId id() const override { return DomainId::Overlays; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
};

// Audio RSP task state. Accepted tasks are drained at Frozen; DMEM persists.
class RspDomain final : public Domain {
public:
    DomainId id() const override { return DomainId::Rsp; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
};

// P3 semantic state plus P3.1 GPU-authoritative color/depth planes. Import
// keeps the current resolution scale / aspect settings (user configuration).
class RendererDomain final : public Domain {
public:
    DomainId id() const override { return DomainId::Renderer; }
    uint32_t extra_domains() const override { return domain_bit(DomainId::Color) | domain_bit(DomainId::Depth); }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
};
}
