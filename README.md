# EcuParser checksum yamasi

## Sorun

`293-822_stage1_modified.bin` araca yuklendiginde immobilizer isigi yandi
ve arac calismadi. Sebep: EcuParser'in eski "Preserve mode" mantigi yanlis
varsayima dayaliydi. Yorumlarda ECU checksum'unun "soft validate" oldugu
varsayiliyordu (yani modifiye edilen byte'lar icin orijinal CS'i korumak
yeterli olur). Gercekte Bosch EDC15C 28F0_100 ECU'su her boot'ta CS'i hard
validate ediyor - bu yuzden modify edilen Block B icin CS yeniden
hesaplanmasi gerekiyordu, ama eski kod onu orijinalden koruyordu.

`293-822_stage1_5_egr_off.bin` ise calisiyor cunku onun CS'i (0x07BD7C
adresinde) WinOLS / ECM Titanium gibi ticari bir arac tarafindan dogru
hesaplanmis. Bu araclar Bosch'un proprietary CRC algoritmasinin
implementasyonuna sahip.

## Algoritma neden recompute edilemiyor

Bosch EDC15C calibration CRC'si proprietary. Test edilenler:
- Additive sum (byte/word/dword, BE+LE, signed/unsigned)
- CRC-16 polinomlari (0x1021, 0x8005, 0xA001, 0xC867, 0xCDF4, 0x8408)
- CRC-32 (zlib)
- Two's complement, XOR varyantlari
- 5-blok sum chain'leri
- Sum + sabit offset (her plausible aralikta)
- Permute byte-order toplam'lari

Hicbiri eslesmiyor. Acik kaynakta da formul yok - sadece WinOLS, ChkSuite,
ECM Titanium, MPPS, Galletto, ECU.design gibi ticari/online araclar
hesaplayabiliyor. ECM Titanium 1.61 ekraninizda gorunen "Partial CheckSum
calculation" diyalogu sadece analitik metrikleri gosteriyor (byte sum,
even/odd sum, 16/32 bit varyantlari) - bunlarin tamami dogrulandi ama
hicbiri actual ECU CS degil.

## 28F0_100 calibration yapisi (kesfedildi)

Marker bytes "C3 C3 C3 13 C8 + 4 byte CS" pattern'i ile 3 ayri checksum
blogu var:

| Blok | Veri Araligi | CS Adresi | Icerik |
|------|--------------|-----------|--------|
| A | 0x000000..0x013FFB | 0x013FFC | Boot/loader code |
| B | 0x014000..0x07BD7B | 0x07BD7C | Ana kalibrasyon (tum map'ler) |
| C | 0x07BD80..0x07FCFB | 0x07FCFC | Modul ID, version stamp |

Cogu tune sadece Block B'ye dokunur.

## Yeni cozum: Reference Bin yaklasimi

Kod artik export sirasinda her blok icin ayri ayri **strateji** seciyor:

1. **KeepOriginal** — Block bytes orijinal ile birebir aynisi → orijinal
   CS'i koru. (Block A ve C icin tipik durum)
2. **CopyFromReference** — Block bytes referans (calisan) bin ile birebir
   aynisi → referans bin'in CS'ini kopyala. Referans bin, ticari arac ile
   CS'i duzeltilmis ve calisan bir bin olmali.
3. **Unresolvable** — Block bytes hem orijinalden hem referanstan farkli
   → export REDDEDILIR. Kullanici uyarilir, harici checksum corrector
   kullanmasi onerilir.

Bu yapi sayesinde:
- Eski "preserve" yanlis davranisi imkansiz hale geldi
- Kullanici herhangi bir araca/online servise gondermeden once
  EcuParser'da hata uyarisi gorur
- Calisan referans bin'in oldugu durumlarda otomatik olarak dogru CS yazilir

## Yapilan kod degisiklikleri

### `src/core/Checksum.h` ve `src/core/Checksum.cpp`
- Eski `Preserve` algorithm enum'i ve `ProtectedRegion` yapisi kaldirildi
- 3 blokluk `28F0_100` profil (Block A, B, C) tanimlandi
- Yeni API: `evaluate()` ve `applyStrategies()`
- `ChecksumStrategy` enum (KeepOriginal/CopyFromReference/Unresolvable)
- Per-block status raporu

### `src/gui/MainWindow.h` ve `src/gui/MainWindow.cpp`
- `m_refBin` ve `m_refBinPath` alanlari eklendi
- `loadReferenceBin()`, `onBrowseReferenceBin()`, `onClearReferenceBin()`
  metodlari eklendi
- File menusunde "Load reference bin..." (Ctrl+Shift+R) ve "Clear reference
  bin" eylemleri eklendi
- `onExportModifiedBin()` artik `Checksum::applyStrategies` cagiriyor.
  Unresolvable durumunda export reddedilir, kullanici "Save without
  checksum fix" secenegine sahip ama dosya adi otomatik
  "_needs_checksum_fix" suffix'i alir
- Eski `refreshProtectedSnapshots()` yerine `refreshReferenceBinUi()`

### `src/gui/ChecksumDialog.h` ve `src/gui/ChecksumDialog.cpp`
- Tamamen yeniden yazildi: per-block strateji tablosu, "Bytes vs Orig",
  "Bytes vs Ref", "Will write" kolonlari
- Renk kodlu (yesil=Keep, mavi=Copy, kirmizi=Unresolvable)
- "Apply strategies now" undoable (Ctrl+Z)
- Reference bin durumu gosterilir

### `src/gui/main.cpp` (CLI)
- Eski `Checksum::repair` ve `Checksum::verify` API cagrilari kaldirildi
- Yeni `--orig-bin` ve `--ref-bin` CLI secenekleri eklendi
- `--repair-checksum` artik `Checksum::applyStrategies`'i cagiriyor
- Apply-stage yolu da yeni API'ye gecirildi
- Cozumlenmemis blok varsa exit code 1, dosya `_needs_checksum_fix`
  suffix'iyle kaydedilir

## Aracin calismasi icin Turkay'in dosyasi

`293-822_stage1_modified_fixed.bin` asagidaki teknikle uretildi:
- `293-822_stage1_modified.bin`'in working bin ile uyusmayan 433 byte'i
  working seviyesine indirildi (ekstra agresif torque limiter
  artislari (12835/13800) working seviyesine (11460/13454) cekildi)
- Sonuc working bin ile birebir ayni MD5 - yani dogrudan calisir

Dilersen broken bin'in ekstra agresif torque limiter degerlerini geri
alip ayri bir tune yapmak istersen, o zaman EcuParser'da o degisiklikleri
yapip exportta UNRESOLVABLE uyarisi alacaksin - sonra bu dosyayi WinOLS
veya ECM Titanium'a verip CS'i duzelttirmen gerekir.

## Test sonuclari (CLI)

```
# 1) Broken (orig+ref dahil): Block B UNRESOLVABLE
./EcuParser --bin broken.bin --orig-bin orig.bin --ref-bin working.bin \
  --repair-checksum --driver J293_822.drt --out out.bin
> Block B: UNRESOLVABLE (ekstra modify, hicbir ref'le eslesmiyor)
> exit 1

# 2) Working as input + ref: Tum bloklar resolve
./EcuParser --bin working.bin --orig-bin orig.bin --ref-bin working.bin \
  --repair-checksum --driver J293_822.drt --inplace
> Block B: CopyFromReference (already 0xA9D28B83, no write)
> exit 0
> MD5 sonucta degismedi

# 3) Orig + working'in tum map degisiklikleri uygulanmis: tum bloklar resolve
./EcuParser --bin user_edited.bin --orig-bin orig.bin --ref-bin working.bin \
  --repair-checksum --driver J293_822.drt --out final.bin
> Block B: CopyFromReference - WROTE 0xA9D28B83 (was 0xD1AB38E9)
> exit 0
> MD5(final.bin) == MD5(working.bin)  ✓
```

## ECM Titanium metrikleri (dogrulandi)

Tum 11 metrik formulu cikartildi (Checksum.cpp icinde gomulu degil ama
istersen ChecksumDialog'a "Diagnostics" tab'i olarak eklenebilir):

| ECM Field | Formul |
|-----------|--------|
| Checksum (16) | byte_sum & 0xFFFF |
| Compl (16) | ~byte_sum & 0xFFFF |
| Even (16) | even-indexed byte sum & 0xFFFF |
| Odd (16) | odd-indexed byte sum & 0xFFFF |
| 16bit LH | word_sum LE (32-bit) |
| 16bit HL | word_sum BE (32-bit) |
| DWord | byte_sum (32-bit) |
| 32bit #1 | dword_sum BE |
| 32bit #2 | dword_sum perm(1,0,3,2) (16-bit swap) |
| 32bit #3 | dword_sum LE |
| 32bit #4 | dword_sum perm(2,3,0,1) (full byte-swap) |

Bunlar gercek ECU CRC'si DEGIL, sadece harici tool'lar icin diagnostik
metrik. Gercek CRC hala unrecovered.
