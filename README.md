# EcuParser - Qt 6 / C++ GUI

ECM Titanium .drt driver dosyalarını parse eden ve EDC15C bin'lerini ikili
karşılaştırma modunda görselleştiren Qt 6 uygulaması.

## Desteklenen Driver: J293_822 (Jeep WJ 2.7 CRD, EDC15C, schema 28F0_100)

ECM Titanium 1.61 ile birebir karşılaştırılarak doğrulanmış 11 harita.
Adresler ve boyutlar `src/model/DriverNames.cpp` içinde sabit kodlu —
DRT dosyasındaki dim alanları bazı haritalar için yanlış olduğundan
override edilmiştir.

| #  | Harita adı                                          | Adres     | Boyut  |
|----|-----------------------------------------------------|-----------|--------|
| 1  | injection at part throttle                          | 0x076F52  | 16x16  |
| 2  | rail pressure                                       | 0x07ADD2  | 16x16  |
| 3  | injection at part throttle (Map 1)                  | 0x072CF0  | 16x20  |
| 4  | injection at part throttle (Map 2)                  | 0x072FC0  | 16x20  |
| 5  | injection at part throttle (Map 1) (Boost x RPM)    | 0x078C5A  | 16x20  |
| 6  | injection at part throttle (Map 2) (Boost x RPM)    | 0x0791FA  | 16x20  |
| 7  | phase of injection                                  | 0x071F52  | 12x16  |
| 8  | fuel during acceleration                            | 0x07753E  | 12x10  |
| 9  | fuel during acceleration (Map 2)                    | 0x07765E  | 10x10  |
| 10 | turbo pressure                                      | 0x075EA0  | 12x12  |
| 11 | torque limiter                                      | 0x076D82  | 19x1   |

5 ve 6 numaralı haritalar DRT'de 2 instance'lı listeleniyor (ikinci
adresler `0x078F2A` ve `0x0794CA`), ancak ECM Titanium yalnızca ilkini
gösteriyor. Biz de aynı davranışı `maxInstances=1` ile koruyoruz.

11 numaralı torque limiter DRT'de "?" type code ile boş `name` ve
`0x0` boyut alanlarıyla kayıtlı — DriverNames'taki override `19x1`
boyutuyla doğru görüntülenmesini sağlıyor.

5 adet 16-satırlı harita (rail pressure + (Map 1/2) + iki Boost x RPM
varyantı) **aynı RPM eksenini paylaşıyor** ve bu eksen bin'de yok,
driver içine gömülü:

```
700, 800, 900, 1000, 1100, 1300, 1500, 1700,
1900, 2100, 2400, 2700, 3100, 3500, 4000, 4500
```

DriverNames bu RPM ekseni değerlerini hardcoded tutuyor (rail
pressure'ı ECM Titanium ekran görüntüsünden alındı, diğer 4 harita
aynı eksen olarak doğrulandı).

## Decode kuralları (doğrulanmış)

- **Cell endianness: little-endian** (BE değil) - 0x076F52 adresinde
  `0x10 0x0E` = 3600 LE, ECM ile eşleşiyor
- **Cell layout: idx = axisX_row * dimY + axisY_col** (row-major,
  RPM = dış indeks, Load = iç indeks)
- **Axis breakpoints da LE** olarak okunur
- **Cell size: 2 byte (u16)**, 0..65535 aralığında
- **DRT dim alanları her zaman güvenilir değil** — DriverNames
  override'ı dim ve eksen değerlerini per-map sabitleyebiliyor

## Görünüm (ECM Titanium uyumlu)

- **Sidebar (dark)**: ECM tree sırası ve isimleri (J293_822 için)
- **Tablo (light)**: açık zemin, mavi başlıklar, sadece değişen hücreler
  vurgulu (yeşil=artış, pembe=azalış)
- **Grafik (cyan zemin)**: Image 3 tarzı ince mavi/kırmızı çizgi, 2 taraflı
  Y ekseni, "Original = Modified" uyarısı

## Özellikler

**Dosya yönetimi**
- Driver, Original, Modified bin combo'ları (data/ klasöründen otomatik
  doldurulur, başlangıçta hiçbir şey yüklenmez)
- "..." browse butonları ile data/ dışındaki dosyaları seçme
- Original seçildiğinde Modified de aynı dosyaya **otomatik kopyalanır**
  (ECM "Driver" workflow'u) — kullanıcı isterse Modified'ı sonradan
  başka bir bin'e değiştirir
- **Export modified...**: Modified bin'i `<orig>_modified.bin` default
  ismiyle diske kaydeder, Original'i bozmaz

**Düzenleme**
- Hücreye çift tıklayarak doğrudan değer girme (LE u16 olarak yazılır)
- Tabloda alan seç → **sağ tık → Set value...** → tüm seçili hücrelere
  tek değer yazma (bulk edit)
- **Copy ORI → MOD**: seçili haritanın orijinal değerlerini Modified'a
  geri yükle (yanlış yapılan edit'i geri alma)

**Görselleştirme**
- **Tablo**: Light ECM teması, sadece **değişen** hücreler vurgulu
  (yeşil=artış, pembe=azalış), tooltip'te orig→mod delta
- **Grafik**: Cyan zemin, mavi (Original) ve kırmızı (Modified) çizgi,
  iki taraflı Y ekseni 0..65535, X ekseninde **hex adres** etiketleri
- **Mouse cursor crosshair**: Grafik üzerinde mouse hareketinde en yakın
  hücreyi snap eder, cream renkli tooltip'te **RPM / Load / Adres / Ori
  / Mod (delta)** gösterir

## Yapı

```
src/core/   DrtParser, BinFile (LE/BE read+write), MapData (LE row-major)
src/model/  DriverModel, MapDefinition, AxisDefinition, MapCategory,
            DriverNames (ECM canonical name table per driver)
src/gui/    MainWindow, DriverTreeWidget, MapTableWidget (light ECM style),
            MapGraphWidget (cyan ECM style), AppPaths
data/       J293_822.drt + bin'ler
```

## Derleme

`build.sh` script'i proje klasörünü temiz tutar — build çıktıları proje
**dışında**, kardeş `EcuParser-build/` klasörüne yazılır. Pro dosyası
release ve debug çıktılarını ayrı alt klasörlere yönlendirir, böylece
iki konfigürasyon yan yana durabilir:

```bash
./build.sh                              # release build
```

Sonra çalıştır (proje dizinine cd lazım çünkü `data/` klasörünü oradan
arar):

```bash
cd EcuParser
../EcuParser-build/release/EcuParser
```

**Qt Creator** ile çalışıyorsanız: pro dosyası release ve debug
çıktılarını otomatik olarak doğru alt klasörlere koyuyor. Build
directory ayarınızda örneğin `build-EcuParser/` görünüyorsa, exe'ler
şöyle yerleşir:

```
build-EcuParser/
├── Makefile, Makefile.Debug, Makefile.Release
├── debug/EcuParser(.exe)        ← Debug build çıktısı
│   └── .obj, .moc, ...
└── release/EcuParser(.exe)      ← Release build çıktısı
    └── .obj, .moc, ...
```

**Manuel out-of-source** isterseniz:

```bash
mkdir -p ../EcuParser-build && cd ../EcuParser-build
qmake6 ../EcuParser/EcuParser.pro CONFIG+=release
make -j4
# Binary: ./release/EcuParser
```

In-source build (proje içinde `qmake6 EcuParser.pro && make`) hala
çalışır ama proje klasörünü kirletir, out-of-source tavsiye edilir.

## Bilinen sınırlar

- Bazı haritaların DRT dim ve eksen bilgisi yanlış (driver içine gömülü) -
  DriverNames sınıfında her harita için override edilebilir

- Bazı haritaların eksen breakpoint sayısı dim'den farklı (son birkaç
  satır/sütun anlamsız değer gösterebilir)
- Cell değerleri raw u16 LE — fiziksel birime dönüşüm yok (örn. tork byte
  -> Nm, basınç byte -> mbar)
- Sadece u16 hücre yazımı destekleniyor
