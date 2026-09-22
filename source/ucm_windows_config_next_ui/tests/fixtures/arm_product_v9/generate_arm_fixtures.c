/* Run in this directory. Link the original ARM pure-C wire/identity sources.
 * These are offline synthetic states validated by ARM code, not board captures. */
#include "ucm2_sha256.h"
#include "ucm_device_model_control_wire_v2.h"
#include "ucm_measurement_publication_v1.h"
#include "ucm_result_input_status_v1.h"
#include "ucm_system_input_policy_control_wire_v1.h"
#include "ucm_usb_extended_wire_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "failed line %d\n", __LINE__);                                                             \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
static void save(const char *name, const void *b, size_t n)
{
    FILE *f = fopen(name, "wb");
    CHECK(f);
    CHECK(fwrite(b, 1, n, f) == n);
    CHECK(fclose(f) == 0);
}
static size_t load(const char *name, void *b, size_t n)
{
    FILE *f = fopen(name, "rb");
    CHECK(f);
    size_t got = fread(b, 1, n, f);
    CHECK(fclose(f) == 0);
    return got;
}
int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--check-model-request") == 0) {
        uint8_t wire[20000], encoded[20000], identity[32];
        size_t n = load(argv[2], wire, sizeof(wire)), written = 0;
        struct ucm_device_model_control_request_v2 request;
        struct ucm_device_model_configuration_json_v1 model;
        CHECK(ucm_device_model_control_request_decode_v2(wire, n, &request) == 0);
        CHECK(ucm_device_model_control_request_encode_v2(&request, encoded, sizeof(encoded), &written) == 0);
        CHECK(written == n && memcmp(wire, encoded, n) == 0);
        CHECK(ucm_device_model_configuration_json_decode_v1(request.json, request.json_bytes, &model) == 0);
        CHECK(ucm_device_model_configuration_json_identity_v1(&model, identity) == 0);
        CHECK(memcmp(identity, model.effective.configuration_sha256, 32) == 0);
        printf("PASS ARM request decode/re-encode and DCI2 identity: %s\n", argv[2]);
        return 0;
    }
    struct ucm_measurement_context_identity_v1 context = {1, 2, 3, 4, 5, 6, 7};
    struct ucm_measurement_publication_v1 pub;
    CHECK(ucm_measurement_publication_invalid_v1(&context, 16, 1000, 32, 40, 56, 10, &pub) == 0);
    save("publication_maintenance.bin", &pub, sizeof(pub));
    pub.frame_counter = 48;
    pub.reason = 9;
    pub.crc32 = ucm_measurement_publication_crc32_v1(&pub);
    CHECK(ucm_measurement_publication_validate_v1(&pub) == 0);
    save("publication_invalid.bin", &pub, sizeof(pub));
    struct ucm_result_input_status_v1 in = {0};
    in.magic = UCM_RESULT_INPUT_STATUS_MAGIC_V1;
    in.abi_version = 1;
    in.message_bytes = sizeof(in);
    in.flags = 1;
    in.publisher_generation = 16;
    in.published_monotonic_ns = 1000;
    in.session_id = 32;
    in.sequence = 40;
    in.frame_counter = 48;
    in.capture_request_id = 56;
    in.input_capture_sequence = 40;
    in.input_capture_monotonic_ns = 900;
    in.input_bound_monotonic_ns = 950;
    in.configured_mold_mode = 1;
    in.crc32 = ucm_result_input_status_crc32_v1(&in);
    CHECK(ucm_result_input_status_validate_v1(&in));
    CHECK(ucm_result_input_status_matches_publication_v1(&in, &pub));
    save("input_status.bin", &in, sizeof(in));
    pub.flags = 7;
    pub.reason = 0;
    pub.measurement_valid_mask = 15;
    pub.force_rod_valid_mask = 15;
    pub.formal_total_n = 400;
    for (int i = 0; i < 4; ++i)
    {
        pub.t0_sample[i] = 100;
        pub.snr_db[i] = 30;
        pub.force_rod_n[i] = 100;
    }
    pub.crc32 = ucm_measurement_publication_crc32_v1(&pub);
    CHECK(ucm_measurement_publication_validate_v1(&pub) == 0);
    save("publication_valid.bin", &pub, sizeof(pub));
    struct ucm_usb_extended_capabilities_v2 cap;
    CHECK(ucm_usb_extended_capabilities_init_v2(&cap, "arm-offline-fixture-v9") == 0);
    save("capabilities.bin", &cap, sizeof(cap));
    uint8_t wire[20000];
    size_t n;
    struct ucm_device_model_control_document_message_v2 dm = {0};
    dm.document_kind = 1;
    dm.present = 1;
    dm.boot_id = 32;
    dm.authority_generation = 40;
    dm.session_id = 48;
    dm.response_request_id = 56;
    dm.document_generation = 7;
    dm.policy_schema_version = 2;
    dm.policy_device_model_id = 168;
    dm.policy_applicability_id = 1;
    dm.allowed_mold_source_mask = 31;
    for (int i = 0; i < 32; ++i)
        dm.system_package_sha256[i] = 16 + i;
    dm.json_bytes = load("device_model.json", dm.json, sizeof(dm.json));
    struct ucm_device_model_configuration_json_v1 model;
    CHECK(ucm_device_model_configuration_json_decode_v1(dm.json, dm.json_bytes, &model) == 0);
    CHECK(ucm_device_model_configuration_json_identity_v1(&model, dm.configuration_sha256) == 0);
    CHECK(memcmp(model.effective.configuration_sha256, dm.configuration_sha256, 32) == 0);
    ucm2_sha256(dm.json, dm.json_bytes, dm.payload_digest);
    CHECK(ucm_device_model_control_document_encode_v2(&dm, wire, sizeof(wire), &n) == 0);
    save("device_model_active.bin", wire, n);
    dm.document_kind = 2;
    dm.commit_sequence = 8;
    CHECK(ucm_device_model_control_document_encode_v2(&dm, wire, sizeof(wire), &n) == 0);
    save("device_model_startup.bin", wire, n);
    struct ucm_device_model_control_state_message_v2 ds = {0};
    ds.state.state_kind = 5;
    ds.state.boot_id = 32;
    ds.state.authority_generation = 40;
    ds.state.session_id = 48;
    ds.response_request_id = 56;
    ds.policy_schema_version = 2;
    ds.policy_device_model_id = 168;
    ds.policy_applicability_id = 1;
    ds.allowed_mold_source_mask = 31;
    memcpy(ds.system_package_sha256, dm.system_package_sha256, 32);
    ds.state.active_valid = 1;
    ds.state.startup_valid = 1;
    ds.state.active_generation = 7;
    ds.state.startup_generation = 7;
    ds.state.startup_commit_sequence = 8;
    memcpy(ds.state.active_config_id, dm.configuration_sha256, 32);
    memcpy(ds.state.startup_config_id, dm.configuration_sha256, 32);
    memcpy(ds.state.active_payload_digest, dm.payload_digest, 32);
    memcpy(ds.state.startup_payload_digest, dm.payload_digest, 32);
    CHECK(ucm_device_model_control_state_encode_v2(&ds, wire) == 0);
    save("device_model_state.bin", wire, 352);
    struct ucm_system_input_policy_control_document_message_v1 ip = {0};
    ip.document_kind = 1;
    ip.present = 1;
    ip.boot_id = 32;
    ip.authority_generation = 40;
    ip.session_id = 48;
    ip.response_request_id = 56;
    ip.document_generation = 7;
    ip.policy_schema_version = 1;
    ip.allowed_plc_interlock_mask = 7;
    ip.allowed_plc_state_encoding_mask = 3;
    memcpy(ip.system_package_sha256, dm.system_package_sha256, 32);
    ip.json_bytes = load("input_policy.json", ip.json, sizeof(ip.json));
    struct ucm_system_input_policy_configuration_v1 policy;
    CHECK(ucm_system_input_policy_json_decode_v1(ip.json, ip.json_bytes, &policy) == 0);
    CHECK(ucm_system_input_policy_identity_v1(&policy, ip.policy_sha256) == 0);
    CHECK(memcmp(policy.policy_sha256, ip.policy_sha256, 32) == 0);
    ucm2_sha256(ip.json, ip.json_bytes, ip.payload_digest);
    CHECK(ucm_system_input_policy_control_document_encode_v1(&ip, wire, sizeof(wire), &n) == 0);
    save("input_policy_active.bin", wire, n);
    ip.document_kind = 2;
    ip.commit_sequence = 8;
    CHECK(ucm_system_input_policy_control_document_encode_v1(&ip, wire, sizeof(wire), &n) == 0);
    save("input_policy_startup.bin", wire, n);
    struct ucm_system_input_policy_control_state_message_v1 ps = {0};
    ps.state.state_kind = 5;
    ps.state.boot_id = 32;
    ps.state.authority_generation = 40;
    ps.state.session_id = 48;
    ps.response_request_id = 56;
    ps.policy_schema_version = 1;
    ps.allowed_plc_interlock_mask = 7;
    ps.allowed_plc_state_encoding_mask = 3;
    memcpy(ps.system_package_sha256, ip.system_package_sha256, 32);
    ps.state.active_valid = 1;
    ps.state.startup_valid = 1;
    ps.state.active_generation = 7;
    ps.state.startup_generation = 7;
    ps.state.startup_commit_sequence = 8;
    memcpy(ps.state.active_policy_id, ip.policy_sha256, 32);
    memcpy(ps.state.startup_policy_id, ip.policy_sha256, 32);
    memcpy(ps.state.active_payload_digest, ip.payload_digest, 32);
    memcpy(ps.state.startup_payload_digest, ip.payload_digest, 32);
    CHECK(ucm_system_input_policy_control_state_encode_v1(&ps, wire) == 0);
    save("input_policy_state.bin", wire, 352);
    struct ucm_usb_log_catalog_v2 catalog = {0};
    catalog.token = UCM_USB_LOG_CATALOG_TOKEN_V2;
    catalog.abi_version = 2;
    catalog.struct_bytes = sizeof(catalog);
    catalog.catalog_snapshot_id = 1;
    catalog.entry_count = 1;
    catalog.total_entry_count = 1;
    struct ucm_usb_log_entry_v2 *entry = &catalog.entry[0];
    entry->kind = 1;
    entry->flags = 5;
    entry->total_bytes = 680;
    entry->modified_time_ns = 100;
    entry->snapshot_id = 999;
    entry->file_identity_crc32 = 123;
    strcpy(entry->name, "result-20260909-001.urs2");
    catalog.crc32 = ucm_usb_log_catalog_crc32_v2(&catalog);
    CHECK(ucm_usb_log_catalog_validate_v2(&catalog) == 0);
    save("log_catalog.bin", &catalog, sizeof(catalog));
    struct ucm_usb_log_read_chunk_v2 chunk = {0};
    chunk.token = UCM_USB_LOG_READ_TOKEN_V2;
    chunk.abi_version = 2;
    chunk.header_bytes = sizeof(chunk);
    chunk.snapshot_id = 999;
    chunk.total_bytes = 680;
    chunk.chunk_bytes = 680;
    chunk.file_identity_crc32 = 123;
    chunk.crc32 = ucm_usb_log_read_chunk_crc32_v2(&chunk);
    CHECK(ucm_usb_log_read_chunk_validate_v2(&chunk) == 0);
    memset(wire, 0, 744);
    memcpy(wire, &chunk, sizeof(chunk));
    save("log_chunk.bin", wire, 744);
    puts("ARM encoder/validator fixtures generated");
    return 0;
}
