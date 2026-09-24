#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

def die(msg: str) -> None:
    raise SystemExit("V29 contract failure: " + msg)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <V29-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "platformio.ini").read_text()
    mesh_h = (root / "src/MyMesh.h").read_text()
    mesh_cpp = (root / "src/MyMesh.cpp").read_text()
    main_cpp = (root / "src/main.cpp").read_text()
    ui = (root / "src/ui-touch/UITask.cpp").read_text()
    h = (root / "src/helpers/esp32/V29EmergencyFabric.h").read_text()
    cpp = (root / "src/helpers/esp32/V29EmergencyFabric.cpp").read_text()
    portal_h = (root / "src/helpers/esp32/V29EmergencyPortal.h").read_text()
    portal_cpp = (root / "src/helpers/esp32/V29EmergencyPortal.cpp").read_text()

    for flag in (
        "MESH_OFFGRIDNL_V29=1",
        "V29_OFFLINE_CORE=1",
        "V29_EMERGENCY_192H_TARGET=1",
        "V29_MEMORY_TARGET_PERCENT=80",
        "V29_SIMPLE_EMERGENCY_UX=1",
    ):
        if flag not in pio:
            die("missing " + flag)

    for marker in (
        "BODY_MAX == 24",
        "MEMORY_TARGET_PERMILLE = 800",
        "MEMORY_RESERVE_PERMILLE = 200",
        "STORAGE_TARGET_PERMILLE = 800",
        "DEFAULT_MAX_CARRY = 4",
        "LOCAL_FORWARD_LIMIT = 3",
        "CheckInState",
        "PowerMode",
        "setPowerMode",
        "sendCheckIn",
        "sendHelpRequest",
        "takeEvent",
    ):
        if marker not in h:
            die("fabric interface marker missing " + marker)

    for marker in (
        "MOG29-DIRECT-V1",
        "mbedtls_gcm_crypt_and_tag",
        "mbedtls_gcm_auth_decrypt",
        "mesh::Identity signer",
        "signer.verify",
        "_mesh->v27SignGlobal",
        "_mesh->v28CalcSharedSecretAny",
        "SPIFFS.totalBytes",
        "SPIFFS.usedBytes",
        "heap_caps_get_free_size",
        "heap_caps_get_largest_free_block",
        "heap_caps_get_minimum_free_size",
        "v29q0.bin",
        "v29q1.bin",
        "snapshotValid",
        "flushSnapshot",
        "remaining <= maxCarry",
        "PowerMode::Critical",
        "PowerMode::Emergency",
    ):
        if marker not in cpp:
            die("fabric implementation marker missing " + marker)

    for forbidden in (
        "HTTPClient",
        "WiFiClientSecure",
        "V28_RELAY_URL",
        "broker.emqx.io",
        "PubSubClient",
    ):
        if forbidden in h + "\n" + cpp:
            die("V29 offline core must not depend on Internet transport: " + forbidden)

    for marker in (
        "v29SendEmergencyRaw",
        "v29_emergency_fabric.onRawFrame",
    ):
        if marker not in mesh_h + "\n" + mesh_cpp:
            die("MyMesh emergency hook missing " + marker)

    for marker in (
        "v29_emergency_fabric.begin(&the_mesh)",
        "v29_emergency_fabric.loop()",
    ):
        if marker not in main_cpp:
            die("V29 lifecycle missing " + marker)

    for marker in (
        "v29EmergencyHomeCb",
        'make_launcher(TR("Noodmodus")',
        "Ik ben veilig",
        "Ik heb hulp nodig",
        "Stuur bericht",
        "Noodinformatie",
        "Gezin / contacten",
        "Netwerkstatus",
        "112 is niet automatisch gebeld",
        "Werkt lokaal zonder internet",
        "Telefoon verbinden",
        "v29PortalStartApply",
        "Noodcontact meldt: ik ben veilig",
        "Hulpvraag ontvangen via lokaal netwerk",
        "v29_emergency_fabric.takeEvent",
    ):
        if marker not in ui:
            die("simple emergency UI marker missing " + marker)

    primary_ui = ui[ui.find("static void v29EmergencyHomeCb"):ui.find("#endif", ui.find("static void v29EmergencyHomeCb"))]
    for forbidden in ("RSSI", "SNR", "spreading factor", "hop count", "channel key"):
        if forbidden.lower() in primary_ui.lower():
            die("technical jargon leaked into primary emergency UI: " + forbidden)

    for marker in (
        "WiFi.softAP",
        "DNSServer",
        "SESSION_MAX_MS",
        "IDLE_STOP_MS",
        "MeshOffGridNL-",
        "112 is niet automatisch gebeld",
        "sendCheckInToEmergencyContacts",
        "sendHelpToEmergencyContacts",
    ):
        if marker not in portal_h + "\n" + portal_cpp:
            die("local emergency portal marker missing " + marker)

    for forbidden in ("HTTPClient", "WiFiClientSecure", "V28_RELAY_URL", "PubSubClient"):
        if forbidden in portal_h + "\n" + portal_cpp:
            die("local emergency portal must not depend on Internet/cloud: " + forbidden)

    for marker in (
        "const bool v29_portal_active",
        "wifiConfigWantsWifi() || v29_portal_active",
        "wifi_state_machine_active && !v29_portal_active",
        "wifi_started && !v29_portal_active",
    ):
        if marker not in main_cpp:
            die("portal/main Wi-Fi ownership interlock missing " + marker)

    for marker in (
        'STALL_SCOPE("v29-portal", v29_emergency_portal.loop())',
        "Noodinformatie: controleer directe veiligheid",
        "Telefoon verbinden",
    ):
        if marker not in main_cpp + "\n" + ui:
            die("V29 emergency UI/portal runtime marker missing " + marker)

    for marker in (
        "ACTION_RATE_MS",
        "actionAllowed",
        "/moving?",
        "/meeting?",
        "millis() + 300UL",
    ):
        if marker not in portal_h + "\n" + portal_cpp:
            die("V29 portal hardening marker missing " + marker)

    # V28/P1 foundations remain present and unchanged in intent.
    for marker in (
        "MESH_OFFGRIDNL_V28=1",
        "V28_RF_PRIMARY=1",
        "V28_INTERNET_SECONDARY=1",
        "LORA_FREQ=869.618",
        "LORA_BW=62.5",
        "LORA_SF=8",
        "LORA_TX_POWER=22",
        "MAX_LORA_TX_POWER=22",
    ):
        if marker not in pio:
            die("inherited V28/P1 invariant missing " + marker)

    print("V29 contracts OK: offline emergency core, simple UX, 80/20 governor, power modes, local portal, V28/P1 preserved")

if __name__ == "__main__":
    main()
