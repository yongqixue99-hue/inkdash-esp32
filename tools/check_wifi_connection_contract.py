from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"WIFI_CONNECTION_CONTRACT_RED {message}")


def main() -> None:
    client = (PROJECT_ROOT / "src" / "dashboard_client.cpp").read_text(
        encoding="utf-8"
    )
    network = (PROJECT_ROOT / "include" / "network_config.h").read_text(
        encoding="utf-8"
    )

    portal_start = client.index("void DashboardClient::startProvisioningPortal()")
    portal_end = client.index("bool DashboardClient::fetchJsonWithFallback", portal_start)
    portal = client[portal_start:portal_end]

    require(
        "wifi_manager_.startConfigPortal(" in portal,
        "fallback must open the portal directly after the explicit station attempt",
    )
    require(
        "wifi_manager_.autoConnect(" not in portal,
        "fallback must not start a second station connection through autoConnect",
    )
    require(
        "WiFi.disconnect(false, false);" in portal,
        "the timed-out station attempt must be cancelled before SoftAP startup",
    )
    require(
        "kConnectTimeoutMs = 30000" in network,
        "cold boot must allow 30 seconds for the saved network before provisioning",
    )

    print("WIFI_CONNECTION_CONTRACT_OK single-attempt/direct-portal/30s-timeout")


if __name__ == "__main__":
    main()
