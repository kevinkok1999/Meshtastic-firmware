#include "NanoMapFlash.h"
#if defined(ESP32)
#include <esp_partition.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR < 5
#include <esp_spi_flash.h>
#endif

namespace meshtastic::nanomap {
namespace {
const esp_partition_t* internalTiles() {
  const auto* p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      static_cast<esp_partition_subtype_t>(0x83),"tiles");
  return p && p->address==0x7d0000 && p->size==kRawTilePartitionBytes ? p : nullptr;
}
}
TileStorage classifyInternalTiles() {
  const auto* p=internalTiles();
  uint8_t prefix[4];
  if(!p || esp_partition_read(p,0,prefix,sizeof(prefix))!=ESP_OK) return TileStorage::Unavailable;
  return classifyTileHeader(prefix,sizeof(prefix));
}
bool inspectInternalTiles(RawPackVisitor visitor, void* context) {
  const auto* p=internalTiles();
  if(!p || !visitor || classifyInternalTiles()!=TileStorage::NanoMap) return false;
  const void* ptr=nullptr;
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_partition_mmap_handle_t handle{};
  if(esp_partition_mmap(p,0,p->size,ESP_PARTITION_MMAP_DATA,&ptr,&handle)!=ESP_OK) return false;
#else
  spi_flash_mmap_handle_t handle{};
  if(esp_partition_mmap(p,0,p->size,SPI_FLASH_MMAP_DATA,&ptr,&handle)!=ESP_OK) return false;
#endif
  size_t length=0;
  const auto* bytes=static_cast<const uint8_t*>(ptr);
  const bool ok=locateRawImage(bytes,p->size,length)==ImageStatus::Ok && visitor(bytes,length,context);
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_partition_munmap(handle);
#else
  spi_flash_munmap(handle);
#endif
  return ok;
}
}
#endif
