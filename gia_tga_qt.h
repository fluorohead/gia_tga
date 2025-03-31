#ifndef GIA_TGA_QT_H
#define GIA_TGA_QT_H

#include <QtTypes>
#include <QDebug>

namespace gia_tga_qt
{
enum class GIA_TgaErr: size_t { InvalidHeader = 0, ValidHeader = 1, TruncDataAbort = 2, TooMuchPixAbort      = 3,
                                Success       = 4, MemAllocErr = 5, NotInitialized = 6, NeedHeaderValidation = 7,
                                NeedDecoding  = 8 };

enum class GIA_TgaOrigin: quint8 {  TopLeft    = 0b00100000,    TopRight = 0b00110000,
                                    BottomLeft = 0b00000000, BottomRight = 0b00010000,
                                    Unknown    = 0b11111111 // значение при невалидированном заголовке
                                };

#pragma pack(push,1)
struct GIA_TgaHeader
{
    quint8  id_len;
    quint8  cmap_type;
    quint8  img_type;
    quint16 cmap_start;
    quint16 cmap_len;
    quint8  cmap_depth;
    quint16 x_offset;
    quint16 y_offset;
    quint16 width;
    quint16 height;
    quint8  pix_depth;
    quint8  img_descr;
};
struct GIA_TgaExtInfo
{
    QString author;
    QString comment;
    quint16 stamp_month;
    quint16 stamp_day;
    quint16 stamp_year;
    quint16 stamp_hour;
    quint16 stamp_minute;
    quint16 stamp_second;
    QString job;
    quint16 job_hour;
    quint16 job_minute;
    quint16 job_second;
    QString software;
    quint16 ver_num;
    char    ver_lett;
    quint32 key_color;
    quint16 pix_numer;
    quint16 pix_denom;
    quint16 gamma_numer;
    quint16 gamma_denom;
    quint32 color_offset;
    quint32 stamp_offset;
    quint32 scan_offset;
    quint8  attr_type;
};
struct GIA_TgaInfo
{
    int width;
    int height;
    GIA_TgaOrigin origin;
    int pixel_depth; // в битах : 8, 16, 24, 32
    qsizetype bytes_per_line; // размер сканлинии в байтах
    qint64 total_size; // размер массива декодированных данных
    qint8 type;
    QString id_string;
    GIA_TgaExtInfo extended;
};
#pragma pack(pop)

class GIA_TgaDecoder
{
#pragma pack(push,1)
    struct triplet
    {
        quint8 BB, GG, RR;
    };
    union bbggrraa
    {
        struct
        {
            triplet BBGGRR;
            quint8 AA;
        };
        quint32 dword;
    };
    struct footer
    {
        quint32 ext_offset;
        quint32 dev_offset;
        char signature[18];
    };
    struct extensions_area
    {
        quint16 size;
        char    author[41];
        char    comment[324];
        quint16 stamp_month;
        quint16 stamp_day;
        quint16 stamp_year;
        quint16 stamp_hour;
        quint16 stamp_minute;
        quint16 stamp_second;
        char    job[41];
        quint16 job_hour;
        quint16 job_minute;
        quint16 job_second;
        char    software[41];
        quint16 ver_num;
        char    ver_lett;
        quint32 key_color;
        quint16 pix_numer;
        quint16 pix_denom;
        quint16 gamma_numer;
        quint16 gamma_denom;
        quint32 color_offset;
        quint32 stamp_offset;
        quint32 scan_offset;
        quint8  attr_type;
    };
#pragma pack(pop)
private:
    enum class FSM_States: size_t { NotInitialized, Initialized, HeaderValidated, InvalidHeader, DecodedOK, DecodingAbort, NotEnoughMem };
    static const QStringList err_strings;
    static const QSet<quint8> valid_img_types;
    static const QSet<quint8> valid_cmap_depths;
    static const QSet<qint8> valid_pix_depths;
    quint8 *src_array;
    size_t src_size;
    GIA_TgaHeader *header;
    qint64 pix_data_offset;
    quint8 *dst_array; // указатель не раскодированные данные
    qint64 total_size_p; // полный ожидаемый размер раскодированных данных в пикселях
    qint64 total_size_b; // полный ожидаемый размер раскодированных данных в байтах
    bool is_data_detached;
    quint16 width;
    quint16 height;
    qsizetype bytes_per_line;
    GIA_TgaOrigin origin;
    quint8 one_pix_depth; // размер пикселя в битах
    quint8 one_pix_size; // размер пикселя в байтах
    quint8 cmap_elem_depth; // размер элемента палитры в битах
    quint8 cmap_elem_size; // размер элемента палитры в байтах
    quint16 cmap_len; // количество элементов в палитре (от 1 до 256)
    quint8 alpha_bits; // количество бит альфа-канала
    qint8 image_type;
    FSM_States state;
    bbggrraa *color_map;
    qint64 cmap_offset;
    QString id_string;
private:
    GIA_TgaErr create_cmap_256();
    GIA_TgaErr decode_cm_8();
    GIA_TgaErr decode_cm_rle8();
    GIA_TgaErr decode_gr_8();
    GIA_TgaErr decode_gr_rle8();
    GIA_TgaErr decode_tc_15();
    GIA_TgaErr decode_tc_16();
    GIA_TgaErr decode_tc_24();
    GIA_TgaErr decode_tc_32();
    GIA_TgaErr decode_tc_rle15();
    GIA_TgaErr decode_tc_rle16();
    GIA_TgaErr decode_tc_rle24();
    GIA_TgaErr decode_tc_rle32();
    void fill_with_dword(quint32 value, void *dst_start, quint8 count);
    void fill_with_zeroes();
    void dump_to_file(); // для отладки, приватный метод
    void flip_dia(); // переворачивает BottomRight к TopLeft (diagonal flip)
    void flip_ver(); // переворачивает BottomLeft к TopLeft (vertical flip)
    void flip_hor(); // переворачивает TopRight к TopLeft (horizontal flip)
public:
    GIA_TgaDecoder();
    GIA_TgaDecoder(const GIA_TgaDecoder&) = delete;
    GIA_TgaDecoder& operator=(const GIA_TgaDecoder&) = delete;
    ~GIA_TgaDecoder();

    void init(uchar *object_ptr, size_t object_size); // обязательная начальная инициализация
    GIA_TgaErr validate_header(int max_width = 8192, int max_height = 16384); // проверяет заголовок объекта на корректность
    GIA_TgaErr decode(); // выделяет память и декодирует в неё объект
    const QString& err_str(GIA_TgaErr err_code); // возвращает строковую расшифровку ошибки
    GIA_TgaErr detach_data(); // отсоединяет от себя указатель на dst_array
    uchar* data(); // возвращает указатель на dst_array
    GIA_TgaInfo info(); // возвращает свойства tga-объекта
    void flip(); // переворачивает изображение к нормальному, если origin отличается от TopLeft
};

}

#endif // GIA_TGA_QT_H
