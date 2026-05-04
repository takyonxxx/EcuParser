# EcuParser - Qt 6 / C++ GUI

ECM Titanium .drt driver dosyalarını parse eden ve EDC15C bin'lerini ikili
karşılaştırma modunda görselleştiren Qt 6 uygulaması.

## Doğrulanmış değerler

ECM Titanium 1.61 ile birebir karşılaştırılarak doğrulandı:
- **injection at part throttle** (0x076F52, 16x16) - 500 RPM / 6%-100% Load
  satırı 16 hücrenin tamamı eşleşiyor (3600..8650)
- **Cell endianness: little-endian** (BE değil)
- **Cell layout: idx = axisX_row * dimY + axisY_col** (row-major, RPM=outer)

## Görünüm (ECM Titanium uyumlu)

- **Sidebar (dark)**: ECM tree sırası ve isimleri (J293_822 için)
- **Tablo (light)**: açık zemin, mavi başlıklar, sadece değişen hücreler
  vurgulu (yeşil=artış, pembe=azalış)
- **Grafik (cyan zemin)**: Image 3 tarzı ince mavi/kırmızı çizgi, 2 taraflı
  Y ekseni, "Original = Modified" uyarısı

## Özellikler

- Driver, Original, Modified bin combo'ları (data/ klasöründen otomatik)
- Copy ORI -> MOD: seçili haritanın orijinalini modified bin'e kopyala
- Export modified...: değişen bin'i diske kaydet
- Hücre düzenleme (LE u16 olarak yazılır)

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
