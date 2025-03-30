# GIA's TGA Decoder
**Декодер формата Truevision Targa для использования в Qt-приложениях.**

Библиотека сильно завязана на **Qt**, т.к. создавалась с прицелом на использование именно в этом фреймворке. **STL-вариант** будет доступен позже - он находится в процессе разработки.

Причиной создания стало отсутствие поддержки **TGA** в **Qt6** на платформе Windows.
Существующие сторонние библиотеки отдельных авторов также не отвечали моим требованиям : неполная поддержка формата, невозможность работы с байтовым буфером, отсутствие совместимости с **Qt**, устаревший код и т.д.
Большие библиотеки типа **FreeImage** не подходили из-за своей монструозности и сложности подключения к среде разработки **QtCreator**.

На данный момент формату уже около 40 лет. Тем не менее он до сих пор используется, например, в игровой индустрии для хранения текстур.
Формат поддерживает **RLE** - самый элементарный алгоритм сжатия без потерь на основе кодирования повторяющихся пикселов.

Возможно хранение и без сжатия : такой вариант одновременно является, как слабой, так и сильной стороной формата.
Слабой - слишком большой размер. Сильной - данные уже фактически раскодированы, зачастую их достаточно поместить в память "как-есть" и
передать указателем в **OpenGL** или ещё в какой-либо SDK/API. Кроме этого поддерживается прозрачность через альфа-канал, что тоже важно в играх.

Библиотека тестировалась только на **Intel-архитектуре** с порядком байтов **little-endian**. На архитектурах типа **big-endian** правильность работы не гарантируется : могут быть неверно интерпретированы
значения **RGB**, т.к. некоторые методы класса манипулируют не байтами, а двойными словами. Я всегда готов внести коррективы, если найдётся тестировщик с платформой, отличной от **Intel**.

Надо сказать, что сам по себе формат хранит данные в порядке **little-endian**. Это связано с историей его возникновения. **TGA** создавался как формат плат видеозахвата для платформы **IBM PC**.

## Поддерживаемые типы
В заголовке **TGA** предусмотрено поле, указывающее каким образом пиксельные данные хранятся внутри файла.
Существует разница между разрядностью пиксела изображения и разрядностью цвета в цветовой таблице.
Пиксел может задаваться, например, 8 битами, и эти данные будут индексом в цветовой таблице, которая, в свою очередь, может содержать цвета практически любой разрадности (например 24 или 32-бит).
Приэтом эти же 8 бит можно использовать в монохромном изображении (aka в градациях серого) для непосредственного кодирования цвета, т.е. без всяких ссылок на цветовую таблицу.
Кроме того доступно сжатие **RLE**, что порождает ещё больше сочетаний.

### Разработчикам формата TGA были введены следующие типы изображений :

|Значение|Тип изображения|Цветовая таблица|Сжатие|Поддержка библиотекой gia_tga|
|--|--|--|--|--|
|1|С цветовой таблицей|Есть|Нет|Только 24 и 32-битные цветовые таблицы. Разрядность пиксела только 8-бит. Иные разрядности потенциально могут существовать (например 15/16-битные таблицы). В стандарте об этом указано, но в реальном мире мне не удалось найти примеры таких файлов.|
|2|Truecolor|Нет|Нет|Разрядность пикселей 15, 16, 24, 32.|
|3|Монохромное|Нет|Нет|Разрядность пикселей 8-бит.|
|9|С цветовой таблицей|Есть|RLE|Как и тип-1 поддерживаются только 24 и 32-битные таблицы и 8-битные пиксели.|
|10|Truecolor|Нет|RLE|Разрядность пикселей 15, 16, 24, 32.|
|11|Монохромное|Нет|RLE|Разрядность пикселей 8-бит.|

Есть несколько тезисных правил работы с классом **GIA_TgaDecoder** :
- принимает от вас не путь к файлу, а указатель на предварительно считанный в память файл (рекомендуется использовать **memory-mapping**, это упрощает работу и даёт вам свободу действий)
- работает по принципу простейшего **автомата конечных состояний (FSM)**, что в данном случе означает жёстко обозначенную последовательность вызова методов
- результатом работы всегда является буфер раскодированных данных с порядком байтов **QImage::Format_RGBA8888**, на основе которого можно создать объект класса **QImage** или **QPixmap**
- указатель на исходные данные должен быть валидным на всё время использования объекта **GIA_TgaDecoder**













