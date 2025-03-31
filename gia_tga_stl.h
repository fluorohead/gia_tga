#ifndef GIA_TGA_STL_H
#define GIA_TGA_STL_H

#include <vector>
#include <cstdint>
#include <string>
#include <set>

namespace gia_tga_stl
{
using namespace std;
enum class GIA_TgaErr: size_t { InvalidHeader = 0, ValidHeader = 1, TruncDataAbort = 2, TooMuchPixAbort      = 3,
                                Success       = 4, MemAllocErr = 5, NotInitialized = 6, NeedHeaderValidation = 7,
                                NeedDecoding  = 8 };

enum class GIA_TgaOrigin: uint8_t { TopLeft    = 0b00100000,    TopRight = 0b00110000,
                                    BottomLeft = 0b00000000, BottomRight = 0b00010000,
                                    Unknown    = 0b11111111 // значение при невалидированном заголовке
                                    };
#pragma pack(push,1)
struct GIA_TgaHeader
{
    uint8_t  id_len;
    uint8_t  cmap_type;
    uint8_t  img_type;
    uint16_t cmap_start;
    uint16_t cmap_len;
    uint8_t  cmap_depth;
    uint16_t x_offset;
    uint16_t y_offset;
    uint16_t width;
    uint16_t height;
    uint8_t  pix_depth;
    uint8_t  img_descr;
};
struct GIA_TgaExtInfo
{
    string   author;
    string   comment;
    uint16_t stamp_month;
    uint16_t stamp_day;
    uint16_t stamp_year;
    uint16_t stamp_hour;
    uint16_t stamp_minute;
    uint16_t stamp_second;
    string   job;
    uint16_t job_hour;
    uint16_t job_minute;
    uint16_t job_second;
    string   software;
    uint16_t ver_num;
    char     ver_lett;
    uint32_t key_color;
    uint16_t pix_numer;
    uint16_t pix_denom;
    uint16_t gamma_numer;
    uint16_t gamma_denom;
    uint32_t color_offset;
    uint32_t stamp_offset;
    uint32_t scan_offset;
    uint8_t  attr_type;
};
struct GIA_TgaInfo
{
    int width;
    int height;
    GIA_TgaOrigin origin;
    int pixel_depth; // в битах : 8, 16, 24, 32
    int64_t bytes_per_line; // размер сканлинии в байтах
    int64_t total_size; // размер массива декодированных данных
    int8_t type;
    string id_string;
    GIA_TgaExtInfo extended;
};
#pragma pack(pop)


class GIA_TgaDecoder
{
#pragma pack(push,1)
    struct triplet
    {
        uint8_t BB, GG, RR;
    };
    union bbggrraa
    {
        struct
        {
            triplet BBGGRR;
            uint8_t AA;
        };
        uint32_t dword;
    };
    struct footer
    {
        uint32_t ext_offset;
        uint32_t dev_offset;
        char signature[18];
    };
    struct extensions_area
    {
        uint16_t size;
        char     author[41];
        char     comment[324];
        uint16_t stamp_month;
        uint16_t stamp_day;
        uint16_t stamp_year;
        uint16_t stamp_hour;
        uint16_t stamp_minute;
        uint16_t stamp_second;
        char     job[41];
        uint16_t job_hour;
        uint16_t job_minute;
        uint16_t job_second;
        char     software[41];
        uint16_t ver_num;
        char     ver_lett;
        uint32_t key_color;
        uint16_t pix_numer;
        uint16_t pix_denom;
        uint16_t gamma_numer;
        uint16_t gamma_denom;
        uint32_t color_offset;
        uint32_t stamp_offset;
        uint32_t scan_offset;
        uint8_t  attr_type;
    };
#pragma pack(pop)
private:
    enum class FSM_States: size_t { NotInitialized, Initialized, HeaderValidated, InvalidHeader, DecodedOK, DecodingAbort, NotEnoughMem };
    static const vector<string> err_strings;
    static const set<uint8_t> valid_img_types;
    static const set<uint8_t> valid_cmap_depths;
    static const set<int8_t> valid_pix_depths;
    uint8_t *src_array;
    size_t src_size;
    GIA_TgaHeader *header;
    int64_t pix_data_offset;
    uint8_t *dst_array; // указатель не раскодированные данные
    int64_t total_size_p; // полный ожидаемый размер раскодированных данных в пикселях
    int64_t total_size_b; // полный ожидаемый размер раскодированных данных в байтах
    bool is_data_detached;
    uint16_t width;
    uint16_t height;
    int64_t bytes_per_line;
    GIA_TgaOrigin origin;
    uint8_t one_pix_depth; // размер пикселя в битах
    uint8_t one_pix_size; // размер пикселя в байтах
    uint8_t cmap_elem_depth; // размер элемента палитры в битах
    uint8_t cmap_elem_size; // размер элемента палитры в байтах
    uint16_t cmap_len; // количество элементов в палитре (от 1 до 256)
    uint8_t alpha_bits; // количество бит альфа-канала
    int8_t image_type;
    FSM_States state;
    bbggrraa *color_map;
    int64_t cmap_offset;
    string id_string;
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
    void fill_with_dword(uint32_t value, void *dst_start, uint8_t count);
    void fill_with_zeroes();
    void flip_dia(); // переворачивает BottomRight к TopLeft (diagonal flip)
    void flip_ver(); // переворачивает BottomLeft к TopLeft (vertical flip)
    void flip_hor(); // переворачивает TopRight к TopLeft (horizontal flip)
public:
    GIA_TgaDecoder();
    GIA_TgaDecoder(const GIA_TgaDecoder&) = delete;
    GIA_TgaDecoder& operator=(const GIA_TgaDecoder&) = delete;
    ~GIA_TgaDecoder();

    void init(uint8_t *object_ptr, int64_t object_size); // обязательная начальная инициализация
    GIA_TgaErr validate_header(uint16_t max_width = 8192, uint16_t max_height = 16384); // проверяет заголовок объекта на корректность
    GIA_TgaErr decode(); // выделяет память и декодирует в неё объект
    const string& err_str(GIA_TgaErr err_code); // возвращает строковую расшифровку ошибки
    GIA_TgaErr detach_data(); // отсоединяет от себя указатель на dst_array
    uint8_t* data(); // возвращает указатель на dst_array
    GIA_TgaInfo info(); // возвращает свойства tga-объекта
    void flip(); // переворачивает изображение к нормальному, если origin отличается от TopLeft
};

}

#endif // GIA_TGA_STL_H
