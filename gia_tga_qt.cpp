#include "gia_tga_qt.h"
#include <cstring>
#include <QtDebug>
#include <QFile>

namespace gia_tga_qt
{
const QStringList GIA_TgaDecoder::err_strings = {   "format is not valid",
                                                    "format is valid",
                                                    "truncated data during decoding",
                                                    "too much pixels in data, decoding aborted",
                                                    "successfully decoded",
                                                    "memory allocation error",
                                                    "not initialized",
                                                    "need validation before decoding",
                                                    "need to decode before data detaching"
                                                };
const QSet<quint8> GIA_TgaDecoder::valid_img_types = { 1, 2, 3, 9, 10, 11 };
const QSet<quint8> GIA_TgaDecoder::valid_cmap_depths = { 15, 16, 24, 32 };
const QSet<qint8> GIA_TgaDecoder::valid_pix_depths = { 8, 15, 16, 24, 32 };

GIA_TgaDecoder::GIA_TgaDecoder()
{
    state = FSM_States::NotInitialized;
    is_data_detached = false;
    dst_array = nullptr;
}

GIA_TgaDecoder::~GIA_TgaDecoder()
{
    if ( !is_data_detached )
        delete [] dst_array;
}

void GIA_TgaDecoder::init(uchar *object_ptr, size_t object_size)
{
    if ( !is_data_detached ) delete [] dst_array;
    is_data_detached = false;

    src_array = (quint8*)object_ptr;
    src_size = object_size;
    header = (GIA_TgaHeader*)object_ptr;
    pix_data_offset = -1;
    dst_array = nullptr;

    total_size_p = -1;
    total_size_b = -1;
    one_pix_depth = 0;
    one_pix_size = 0;
    width = 0;
    height = 0;
    bytes_per_line = 0;
    origin = GIA_TgaOrigin::Unknown;
    alpha_bits = 0;
    image_type = -1;
    cmap_elem_depth = 0;
    cmap_elem_size = 0;
    cmap_len = 0;
    id_string.clear();

    state = FSM_States::Initialized;
}

// может возвращать ошибки : ValidHeader, InvalidHeader, NotInitialized
GIA_TgaErr GIA_TgaDecoder::validate_header(int max_width, int max_height)
{
    if ( state == FSM_States::NotInitialized ) return GIA_TgaErr::NotInitialized;
    if ( state == FSM_States::InvalidHeader ) return GIA_TgaErr::InvalidHeader;
    bool is_valid = true;
    if ( src_size < sizeof(GIA_TgaHeader) ) // в исходном объекте не хватает места на заголовок
    {
        is_valid = false;
    }
    else
    {
        if ( header->cmap_type > 1 ) is_valid = false; // неизвестный тип цветовой таблицы
        if ( ( header->cmap_type == 1 ) and ( !valid_cmap_depths.contains(header->cmap_depth) ) ) is_valid = false; // есть таблица? проверяем битность её элементов
        if ( !valid_img_types.contains(header->img_type) ) is_valid = false;
        if ( !valid_pix_depths.contains(header->pix_depth) ) is_valid = false;
        if ( ( header->width == 0 ) or ( header->height == 0 ) ) is_valid = false;
    }
    if ( is_valid )
    {
        if ( ( header->img_type == 2 ) or ( header->img_type == 10 ) )
        {
            quint8 alpha = header->img_descr & 0b00001111;
            if ( ( header->pix_depth == 15 ) and ( alpha > 0 ) ) is_valid = false;
            if ( ( header->pix_depth == 16 ) and ( alpha > 1 ) ) is_valid = false;
            if ( ( header->pix_depth == 24 ) and ( alpha > 0 ) ) is_valid = false;
        }
        if ( ( header->img_type == 3 ) or ( header->img_type == 11 ) )
        {
            if ( header->pix_depth != 8 ) is_valid = false;
        }
        if ( header->img_type == 9 )
        {
            if ( header->cmap_type != 1 ) is_valid = false;
            if ( header->pix_depth != 8 ) is_valid = false;
            if ( ( header->cmap_depth != 24 ) and ( header->cmap_depth != 32 ) ) is_valid = false;
            if ( header->cmap_len > 256 ) is_valid = false;
        }
    }
    if ( is_valid )
    {
        cmap_offset = sizeof(GIA_TgaHeader) + header->id_len;
        pix_data_offset = cmap_offset + header->cmap_type * (header->cmap_len * (header->cmap_depth / 8));
        if ( src_size < pix_data_offset )
        {
            pix_data_offset = -1;
            cmap_offset = -1;
            is_valid = false;
        }
    }
    if ( ( header->width > max_width ) or ( header->height > max_height ) ) is_valid = false;
    if ( is_valid )
    {
        one_pix_depth = header->pix_depth;
        one_pix_size = one_pix_depth / 8;
        width = header->width;
        height = header->height;
        bytes_per_line = width * 4; // раскодирование всегда в формат 0xAARRGGBB (little-endian)
        total_size_p = width * height;
        total_size_b = total_size_p * 4; // изображение любого типа всегда раскодируется в формат 0xAARRGGBB; в памяти (и файле) лежит так : BB GG RR AA
        origin = GIA_TgaOrigin(header->img_descr & 0b00110000);
        alpha_bits = header->img_descr & 0b00001111;
        image_type = header->img_type;
        cmap_elem_depth = header->cmap_depth;
        cmap_elem_size = cmap_elem_depth / 8;
        cmap_len = header->cmap_len;
        id_string.clear();
        for(quint8 id_idx; id_idx < header->id_len; ++id_idx)
        {
            auto ch = src_array[sizeof(GIA_TgaHeader) + id_idx];
            if ( !ch ) break;
            id_string += QChar(ch);
        }

    }
    state = is_valid ? FSM_States::HeaderValidated : FSM_States::InvalidHeader;
    return is_valid ? GIA_TgaErr::ValidHeader : GIA_TgaErr::InvalidHeader;
}

const QString& GIA_TgaDecoder::err_str(GIA_TgaErr err_code)
{
    return err_strings[size_t(err_code)];
}

// может возвращать коды ошибок : NeedDecoding, Success
GIA_TgaErr GIA_TgaDecoder::detach_data()
{
    if ( ( state == FSM_States::DecodedOK ) or ( state == FSM_States::DecodingAbort ) )
    {
        is_data_detached = true;
        return GIA_TgaErr::Success;
    }
    else
    {
        return GIA_TgaErr::NeedDecoding;
    }
}

uchar *GIA_TgaDecoder::data()
{
    return dst_array;
}

GIA_TgaInfo GIA_TgaDecoder::info()
{
    bool is_footer_valid = true;
    qint64 footer_offset = src_size - sizeof(footer);
    footer *ftr;
    extensions_area *ext_area;
    QString author_str, comment_str, job_str, software_str;
    if ( footer_offset <= pix_data_offset ) goto w_o_footer; // сигнатура футера не поместится в файл
    if ( memcmp(&(((footer*)&src_array[footer_offset])->signature), "TRUEVISION-XFILE\x2E\x00", 18) != 0 ) goto w_o_footer; // если 0 - сигнатура совпала
    ftr = (footer*)&src_array[footer_offset];
    if ( ftr->ext_offset < pix_data_offset ) goto w_o_footer; // неверное смещение; либо если 0, значит области расширений нет
    if ( ftr->ext_offset > src_size ) goto w_o_footer; // неверное смещение
    if ( src_size - ftr->ext_offset < sizeof(extensions_area) ) goto w_o_footer; // зона расширений не помещается в файл
    ext_area = (extensions_area*)&src_array[ftr->ext_offset];
    if ( ext_area->size < sizeof(extensions_area) ) goto w_o_footer; // неизвестный размер, лучше не пытаться прочитать такую область

    author_str = QString::fromLocal8Bit(ext_area->author, sizeof(extensions_area::author));
    author_str.chop(author_str.length() - author_str.indexOf('\x00')); // ищем где появляется нулевой символ и откусываем всё, что после него

    comment_str = QString::fromLocal8Bit(ext_area->comment, sizeof(extensions_area::comment));
    comment_str.chop(comment_str.length() - comment_str.indexOf('\x00')); // ищем где появляется нулевой символ и откусываем всё, что после него

    job_str = QString::fromLocal8Bit(ext_area->job, sizeof(extensions_area::job));
    job_str.chop(job_str.length() - job_str.indexOf('\x00')); // ищем где появляется нулевой символ и откусываем всё, что после него

    software_str = QString::fromLocal8Bit(ext_area->software, sizeof(extensions_area::software));
    software_str.chop(software_str.length() - software_str.indexOf('\x00')); // ищем где появляется нулевой символ и откусываем всё, что после него

w_footer:
    return GIA_TgaInfo{
                        width,
                        height,
                        origin,
                        one_pix_depth,
                        bytes_per_line,
                        total_size_b,
                        image_type,
                        id_string,
                        {   author_str,
                            comment_str,
                            ext_area->stamp_month,
                            ext_area->stamp_day,
                            ext_area->stamp_year,
                            ext_area->stamp_hour,
                            ext_area->stamp_minute,
                            ext_area->stamp_second,
                            job_str,
                            ext_area->job_hour,
                            ext_area->job_minute,
                            ext_area->job_second,
                            software_str,
                            ext_area->ver_num,
                            ext_area->ver_lett,
                            ext_area->key_color,
                            ext_area->pix_numer,
                            ext_area->pix_denom,
                            ext_area->gamma_numer,
                            ext_area->gamma_denom,
                            ext_area->color_offset,
                            ext_area->stamp_offset,
                            ext_area->scan_offset,
                            ext_area->attr_type }
                        };

w_o_footer:
    return GIA_TgaInfo{
                        width,
                        height,
                        origin,
                        one_pix_depth,
                        bytes_per_line,
                        total_size_b,
                        image_type,
                        id_string,
                        { "", "", 0, 0, 0, 0, 0, 0, "", 0, 0, 0, "", 0, '\x00', 0, 0, 0, 0, 0, 0, 0, 0, 0 }
                        };
}

void GIA_TgaDecoder::flip_dia()
{
    auto array = (quint32*)dst_array;
    quint32 swap_pixel;
    qint64 half_size = total_size_p / 2;
    qint64 last_idx = total_size_p;
    for(qint64 fwd_idx = 0; fwd_idx < half_size; ++fwd_idx)
    {
        --last_idx;
        swap_pixel = array[fwd_idx];
        array[fwd_idx] = array[last_idx];
        array[last_idx] = swap_pixel;
    }
}

void GIA_TgaDecoder::flip_ver()
{
    auto array = (quint32*)dst_array;
    quint32 swap_pixel;
    quint16 half_fwd_scln = height / 2; // половина сканлиний
    quint16 btm_scln = height; // нижняя сканлиния
    quint32 *scln_fwd_ptr;
    quint32 *scln_btm_ptr;
    for(quint16 fwd_scln = 0; fwd_scln < half_fwd_scln; ++fwd_scln) // начинаем сверху по сканлиниям и до половины изображения
    {
        --btm_scln;
        scln_fwd_ptr = &array[fwd_scln * width]; // указатель на верхнюю сканлинию
        scln_btm_ptr = &array[btm_scln * width]; // указатель на нижнюю сканлинию
        for(qint64 pix_idx = 0; pix_idx < width; ++pix_idx) // идём по пикселям внутри сканлинии слева направо
        {
            swap_pixel = scln_fwd_ptr[pix_idx];
            scln_fwd_ptr[pix_idx] = scln_btm_ptr[pix_idx];
            scln_btm_ptr[pix_idx] = swap_pixel;
        }
    }
}

void GIA_TgaDecoder::flip_hor()
{
    auto array = (quint32*)dst_array;
    quint32 swap_pixel;
    quint32 *scln_ptr;
    quint16 half_scln = width / 2; // половина сканлинии
    quint16 rpix_idx;
    for(quint16 scln = 0; scln < height; ++scln) // идём по всем сканлиниям сверху вниз
    {
        scln_ptr = &array[scln * width]; // указатель на текущую сканлинию
        rpix_idx = width;
        for(quint16 lpix_idx = 0; lpix_idx < half_scln; ++lpix_idx)
        {
            --rpix_idx;
            swap_pixel = scln_ptr[lpix_idx];
            scln_ptr[lpix_idx] = scln_ptr[rpix_idx];
            scln_ptr[rpix_idx] = swap_pixel;
        }
    }
}

void GIA_TgaDecoder::flip()
{
    if ( ( is_data_detached ) or ( dst_array == nullptr ) ) return;
    switch(origin)
    {
    case GIA_TgaOrigin::TopRight:
        flip_hor();
        break;
    case GIA_TgaOrigin::BottomLeft:
        flip_ver();
        break;
    case GIA_TgaOrigin::BottomRight:
        flip_dia();
        break;
    case GIA_TgaOrigin::TopLeft:
    case GIA_TgaOrigin::Unknown:
        break;
    }
}

inline void GIA_TgaDecoder::fill_with_dword(quint32 value, void *dst_start, quint8 count)
{
    auto dst_as_dwords = (quint32*)dst_start;
    for(quint8 idx = 0; idx < count; ++idx)
    {
        dst_as_dwords[idx] = value;
    }
}

void GIA_TgaDecoder::fill_with_zeroes()
{
    auto qwords_array = (quint64*)dst_array;
    qint64 max_pix_duplets = total_size_p / 2; // количество двойных пикселей
    for(qint64 qw_idx = 0; qw_idx < max_pix_duplets; ++qw_idx) // идём по двойным пикселям
    {
        qwords_array[qw_idx] = 0xFF000000'FF000000;
    }
    if ( total_size_p % 2 ) // остался одиночный пиксель?
    {
        *((quint32*)&qwords_array[max_pix_duplets]) = 0xFF000000;
    }
}

void GIA_TgaDecoder::dump_to_file()
{
    QFile file(R"(c:\Downloads\dump01.dat)");
    file.open(QIODeviceBase::ReadWrite);
    file.resize(total_size_b);
    uchar *mmf_ptr = file.map(0, file.size());

    std::memcpy(mmf_ptr, dst_array, total_size_b);

    file.unmap(mmf_ptr);
    file.close();
}

// может возвращать ошибки : TruncDataAbort, TooMuchPixAbort, Success, MemAllocErr, NeedHeaderValidation
GIA_TgaErr GIA_TgaDecoder::decode()
{
    if ( state != FSM_States::HeaderValidated ) return GIA_TgaErr::NeedHeaderValidation;

    if ( !is_data_detached ) delete [] dst_array;
    is_data_detached = false;

    dst_array = new (std::nothrow) quint8[total_size_b];

    if ( dst_array == nullptr )
    {
        return GIA_TgaErr::MemAllocErr;
    }

    fill_with_zeroes(); // обнуление dst_array

    switch(image_type)
    {
    case 1: // non-rle colormapped
    {
        return decode_cm_8();
    }
    case 2: // non-rle truecolor
    {
        switch(one_pix_depth)
        {
        case 15:
            return decode_tc_15();
        case 16:
            return decode_tc_16();
        case 24:
            return decode_tc_24();
        case 32:
            return decode_tc_32();
        }
    }
    case 3: // non-rle grayscale
    {
        return decode_gr_8();
    }
    case 10: // rle truecolor
    {
        switch(one_pix_depth)
        {
        case 15:
            return decode_tc_rle15();
        case 16:
            return decode_tc_rle16();
        case 24:
            return decode_tc_rle24();
        case 32:
            return decode_tc_rle32();
        }
    }
    case 9: // rle colormapped
    {
        return decode_cm_rle8();
    }
    case 11: // rle grayscale
    {
        return decode_gr_rle8();
    }
    default:
    {
        return GIA_TgaErr::InvalidHeader;
    }
    }
}

// может возвращать ошибки : MemAllocErr, Success
GIA_TgaErr GIA_TgaDecoder::create_cmap_256()
{
    color_map = new (std::nothrow) bbggrraa[256];
    if ( color_map == nullptr )
    {
        return GIA_TgaErr::MemAllocErr;
    }

    /// обнуление палитры (потому что в файле она может быть короче 256 элементов)
    for(quint16 cm_dw_idx = 0; cm_dw_idx < 32; ++cm_dw_idx)
    {
        ((quint64*)color_map)[cm_dw_idx] = 0xFF000000FF000000;
    }
    switch(cmap_elem_depth)
    {
    case 24:
    {
        auto trp_cm_array = (triplet*)&src_array[cmap_offset];
        for(quint16 cm_idx = 0; cm_idx < cmap_len; ++cm_idx)
        {
            color_map[cm_idx].BBGGRR = trp_cm_array[cm_idx];
            color_map[cm_idx].AA = 0xFF;
        }
        break;
    }
    case 32:
    {
        auto dw_cm_array = (bbggrraa*)&src_array[cmap_offset];
        for(quint16 cm_idx = 0; cm_idx < cmap_len; ++cm_idx)
        {
            color_map[cm_idx] = dw_cm_array[cm_idx];
        }
        break;
    }
    }
    return GIA_TgaErr::Success;
}

GIA_TgaErr GIA_TgaDecoder::decode_cm_8()
{
    if ( create_cmap_256() == GIA_TgaErr::MemAllocErr )
    {
        state = FSM_States::NotEnoughMem;
        return GIA_TgaErr::MemAllocErr;
    }
    qint64 need_src_size = width * height; // требуемое количество исходных байт
    qint64 remain_size = src_size - pix_data_offset; // фактическое количество исходных байт
    bool truncated = remain_size < need_src_size;
    if ( !truncated ) remain_size = need_src_size;
    quint8 *src_b_array = &src_array[pix_data_offset]; // bytes array
    bbggrraa four_bytes;
    four_bytes.AA = 0xFF;
    auto dst_dw_array = (quint32*)dst_array; // destination dwords array
    for(qint64 b_idx; b_idx < remain_size; ++b_idx)
    {
        dst_dw_array[b_idx] = color_map[src_b_array[b_idx]].dword;
    }
    delete [] color_map;
    if ( truncated )
    {
        state = FSM_States::DecodingAbort;
        return GIA_TgaErr::TruncDataAbort;
    }
    else
    {
        state = FSM_States::DecodedOK;
        return GIA_TgaErr::Success;
    }
}

GIA_TgaErr GIA_TgaDecoder::decode_cm_rle8()
{
    if ( create_cmap_256() == GIA_TgaErr::MemAllocErr )
    {
        state = FSM_States::NotEnoughMem;
        return GIA_TgaErr::MemAllocErr;
    }
    quint8 *rle_array = &src_array[pix_data_offset];
    qint64 rle_size = src_size - pix_data_offset; // rle array size (from pix_data_offset to the end of source file)
    qint64 src_idx = 0; // byte index in rle_array
    qint64 dst_idx = 0; // byte index in dst_array
    qint64 pix_cnt = 0; // decoded pixels counter
    qint64 group_cnt; // group counter for rle or non-rle pixels
    do {
        /// хватает ли места для очередного счётчика группы ?
        if ( rle_size - src_idx < 1 ) { delete [] color_map; state = FSM_States::DecodingAbort; return GIA_TgaErr::TruncDataAbort; } // досрочный выход из цикла : нехватка байтов исходных данных

        group_cnt = (rle_array[src_idx] & 0b01111111) + 1; // счётчик группы всегда кодирует минимум 1 пиксель
        pix_cnt += group_cnt; // обновляем общий счётчик пикселей
        if ( pix_cnt > total_size_p ) { delete [] color_map; state = FSM_States::DecodingAbort; return GIA_TgaErr::TooMuchPixAbort; } // досрочный выход : вылезли за пределы размеров изображения, некорректные rle-данные

        if ( (rle_array[src_idx] >> 7) == 1 ) // пакет rle-группы
        {
            ++src_idx; // перестановка на байт пикселя
            if ( rle_size - src_idx >= 1 ) // хватает ли места в исходном буфере на 1 байт пикселя?
            {
                /// мультипликация байтов пикселя
                fill_with_dword(color_map[rle_array[src_idx]].dword, &dst_array[dst_idx], group_cnt);
                ///
                src_idx += 1; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                delete [] color_map;
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }
        else // пакет не-rle группы
        {
            ++src_idx; // перестановка на байты пикселя
            if ( rle_size - src_idx >= group_cnt ) // хватает ли места в исходном буфере на group_cnt байтов пикселей?
            {
                /// копирование байтов пикселей
                auto src_b_array = (quint8*)&rle_array[src_idx]; // array of bytes
                for(qint64 b_idx = 0; b_idx < group_cnt; ++b_idx)
                {
                    *((quint32*)&dst_array[dst_idx + (b_idx << 2)]) = color_map[src_b_array[b_idx]].dword; // b_idx*4
                }
                ///
                src_idx += group_cnt; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                delete [] color_map;
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }

        dst_idx += (group_cnt << 2); // обновляем общий счётчик байтов (он же индекс следующей позиции для заполнения в dst_array) : сдвиг влево на 2 = *4

    } while(pix_cnt < total_size_p); // декодировали пикселей столько, сколько должны => конец цикла

    delete [] color_map;
    state = FSM_States::DecodedOK;
    return GIA_TgaErr::Success;
}

GIA_TgaErr GIA_TgaDecoder::decode_gr_8()
{
    qint64 need_src_size = width * height; // требуемое количество исходных байт
    qint64 remain_size = src_size - pix_data_offset; // фактическое количество исходных байт
    bool truncated = remain_size < need_src_size;
    if ( !truncated ) remain_size = need_src_size;
    quint8 *src_b_array = &src_array[pix_data_offset]; // bytes array
    bbggrraa four_bytes;
    four_bytes.AA = 0xFF;
    auto dst_dw_array = (quint32*)dst_array; // destination dwords array
    for(qint64 b_idx; b_idx < remain_size; ++b_idx)
    {
        four_bytes.BBGGRR.BB = src_b_array[b_idx];
        four_bytes.BBGGRR.GG = four_bytes.BBGGRR.BB;
        four_bytes.BBGGRR.RR = four_bytes.BBGGRR.BB;
        dst_dw_array[b_idx] = four_bytes.dword;
    }
    if ( truncated )
    {
        state = FSM_States::DecodingAbort;
        return GIA_TgaErr::TruncDataAbort;
    }
    else
    {
        state = FSM_States::DecodedOK;
        return GIA_TgaErr::Success;
    }
}

GIA_TgaErr GIA_TgaDecoder::decode_gr_rle8()
{
    quint8 *rle_array = &src_array[pix_data_offset];
    qint64 rle_size = src_size - pix_data_offset; // rle array size (from pix_data_offset to the end of source file)
    qint64 src_idx = 0; // byte index in rle_array
    qint64 dst_idx = 0; // byte index in dst_array
    qint64 pix_cnt = 0; // decoded pixels counter
    qint64 group_cnt; // group counter for rle or non-rle pixels
    bbggrraa four_bytes;
    four_bytes.AA = 0xFF;
    do {
        /// хватает ли места для очередного счётчика группы ?
        if ( rle_size - src_idx < 1 ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TruncDataAbort; } // досрочный выход из цикла : нехватка байтов исходных данных

        group_cnt = (rle_array[src_idx] & 0b01111111) + 1; // счётчик группы всегда кодирует минимум 1 пиксель
        pix_cnt += group_cnt; // обновляем общий счётчик пикселей
        if ( pix_cnt > total_size_p ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TooMuchPixAbort; } // досрочный выход : вылезли за пределы размеров изображения, некорректные rle-данные

        if ( (rle_array[src_idx] >> 7) == 1 ) // пакет rle-группы
        {
            ++src_idx; // перестановка на байт пикселя
            if ( rle_size - src_idx >= 1 ) // хватает ли места в исходном буфере на 1 байт пикселя?
            {
                /// мультипликация байтов пикселя
                four_bytes.BBGGRR.BB = rle_array[src_idx];
                four_bytes.BBGGRR.GG = four_bytes.BBGGRR.BB;
                four_bytes.BBGGRR.RR = four_bytes.BBGGRR.BB;
                fill_with_dword(four_bytes.dword, &dst_array[dst_idx], group_cnt);
                ///
                src_idx += 1; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }
        else // пакет не-rle группы
        {
            ++src_idx; // перестановка на байты пикселя
            if ( rle_size - src_idx >= group_cnt ) // хватает ли места в исходном буфере на group_cnt байтов пикселей?
            {
                /// копирование байтов пикселей
                auto src_b_array = (quint8*)&rle_array[src_idx]; // array of bytes
                for(qint64 b_idx = 0; b_idx < group_cnt; ++b_idx)
                {
                    four_bytes.BBGGRR.BB = src_b_array[b_idx];
                    four_bytes.BBGGRR.GG = four_bytes.BBGGRR.BB;
                    four_bytes.BBGGRR.RR = four_bytes.BBGGRR.BB;
                    *((quint32*)&dst_array[dst_idx + (b_idx << 2)]) = four_bytes.dword; // b_idx*4
                }
                ///
                src_idx += group_cnt; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }

        dst_idx += (group_cnt << 2); // обновляем общий счётчик байтов (он же индекс следующей позиции для заполнения в dst_array) : сдвиг влево на 2 = *4

    } while(pix_cnt < total_size_p); // декодировали пикселей столько, сколько должны => конец цикла

    state = FSM_States::DecodedOK;
    return GIA_TgaErr::Success;
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_15()
{
    qint64 need_src_size = (width * height) << 1; // требуемое количество исходных байт : (w*h*2)
    qint64 remain_size = src_size - pix_data_offset; // фактическое количество исходных байт
    bool truncated = remain_size < need_src_size;
    if ( !truncated ) remain_size = need_src_size;
    qint64 calc_size = remain_size & 0xFFFFFFFE; // нормализация размера исходных данных к границе 2 байт (обнуление 0 бита)
    qint64 max_words = calc_size >> 1;
    bbggrraa four_bytes;
    four_bytes.AA = 0xFF;
    quint8 blue, green, red;
    auto src_w_array = (quint16*)&src_array[pix_data_offset]; // source words array
    auto dst_dw_array = (quint32*)dst_array; // destination dwords array
    for(qint64 w_idx = 0; w_idx < max_words; ++w_idx)
    {
        blue = (quint8) ( src_w_array[w_idx] & 0b00000000'00011111 );
        green = (quint8) ( ( src_w_array[w_idx] >> 5 ) & 0b00000000'00011111 );
        red = (quint8) ( ( src_w_array[w_idx] >> 10 ) & 0b00000000'00011111 );
        four_bytes.BBGGRR.BB = ( blue << 3 ) | ( blue >> 2 );
        four_bytes.BBGGRR.GG = ( green << 3 ) | ( green >> 2 );
        four_bytes.BBGGRR.RR = ( red << 3 ) | ( red >> 2 );
        dst_dw_array[w_idx] = four_bytes.dword;
    }
    if ( truncated )
    {
        state = FSM_States::DecodingAbort;
        return GIA_TgaErr::TruncDataAbort;
    }
    else
    {
        state = FSM_States::DecodedOK;
        return GIA_TgaErr::Success;
    }
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_16()
{
    qint64 need_src_size = (width * height) << 1; // требуемое количество исходных байт : (w*h*2)
    qint64 remain_size = src_size - pix_data_offset; // фактическое количество исходных байт
    bool truncated = remain_size < need_src_size;
    if ( !truncated ) remain_size = need_src_size;
    qint64 calc_size = remain_size & 0xFFFFFFFE; // нормализация размера исходных данных к границе 2 байт (обнуление 0 бита)
    qint64 max_words = calc_size >> 1;
    bbggrraa four_bytes;
    quint8 blue, green, red;
    auto src_w_array = (quint16*)&src_array[pix_data_offset]; // source words array
    auto dst_dw_array = (quint32*)dst_array; // destination dwords array
    for(qint64 w_idx = 0; w_idx < max_words; ++w_idx)
    {
        blue = (quint8) ( src_w_array[w_idx] & 0b00000000'00011111 );
        green = (quint8) ( ( src_w_array[w_idx] >> 5 ) & 0b00000000'00011111 );
        red = (quint8) ( ( src_w_array[w_idx] >> 10 ) & 0b00000000'00011111 );
        four_bytes.BBGGRR.BB = ( blue << 3 ) | ( blue >> 2 );
        four_bytes.BBGGRR.GG = ( green << 3 ) | ( green >> 2 );
        four_bytes.BBGGRR.RR = ( red << 3 ) | ( red >> 2 );
        four_bytes.AA = ( (src_w_array[w_idx] & 0b10000000'00000000) == 0b10000000'00000000 ) ? 0 : 255;
        dst_dw_array[w_idx] = four_bytes.dword;
    }
    if ( truncated )
    {
        state = FSM_States::DecodingAbort;
        return GIA_TgaErr::TruncDataAbort;
    }
    else
    {
        state = FSM_States::DecodedOK;
        return GIA_TgaErr::Success;
    }
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_24()
{
    qint64 need_src_size = width * height * 3; // требуемое количество исходных байт
    qint64 remain_size = src_size - pix_data_offset;
    bool truncated = remain_size < need_src_size;
    if ( !truncated ) remain_size = need_src_size;
    qint64 calc_size = (remain_size / 3) * 3; // нормализация размера исходных данных к границе 3 байт
    qint64 max_triplets = calc_size / 3;
    bbggrraa four_bytes;
    four_bytes.AA = 0xFF;
    auto src_trp_array = (triplet*)&src_array[pix_data_offset]; // source triplets array
    auto dst_dw_array = (quint32*)dst_array; // destination dwords array
    for(qint64 trp_idx = 0; trp_idx < max_triplets; ++trp_idx)
    {
        four_bytes.BBGGRR = src_trp_array[trp_idx];
        dst_dw_array[trp_idx] = four_bytes.dword;
    }
    if ( truncated )
    {
        state = FSM_States::DecodingAbort;
        return GIA_TgaErr::TruncDataAbort;
    }
    else
    {
        state = FSM_States::DecodedOK;
        return GIA_TgaErr::Success;
    }
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_32()
{
    qint64 remain_size = src_size - pix_data_offset;
    bool truncated = remain_size < total_size_b;
    if ( !truncated ) remain_size = total_size_b;
    qint64 calc_size = remain_size & 0xFFFFFFFC; // нормализация размера исходных данных к границе 4 байт (обнуление 2 младших битов)
    std::memcpy(dst_array, &src_array[pix_data_offset], calc_size);
    if ( truncated )
    {
        state = FSM_States::DecodingAbort;
        return GIA_TgaErr::TruncDataAbort;
    }
    else
    {
        state = FSM_States::DecodedOK;
        return GIA_TgaErr::Success;
    }
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_rle15()
{
    quint8 *rle_array = &src_array[pix_data_offset];
    qint64 rle_size = src_size - pix_data_offset; // rle array size (from pix_data_offset to the end of source file)
    qint64 src_idx = 0; // byte index in rle_array
    qint64 dst_idx = 0; // byte index in dst_array
    qint64 pix_cnt = 0; // decoded pixels counter
    qint64 group_cnt; // group counter for rle or non-rle pixels
    qint64 not_packed_bytes;
    bbggrraa four_bytes;
    quint8 blue, green, red;
    four_bytes.AA = 0xFF;
    do {
        /// хватает ли места для очередного счётчика группы ?
        if ( rle_size - src_idx < 1 ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TruncDataAbort; } // досрочный выход из цикла : нехватка байтов исходных данных

        group_cnt = (rle_array[src_idx] & 0b01111111) + 1; // счётчик группы всегда кодирует минимум 1 пиксель
        pix_cnt += group_cnt; // обновляем общий счётчик пикселей
        if ( pix_cnt > total_size_p ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TooMuchPixAbort; } // досрочный выход : вылезли за пределы размеров изображения, некорректные rle-данные

        if ( (rle_array[src_idx] >> 7) == 1 ) // пакет rle-группы
        {
            ++src_idx; // перестановка на байты пикселя
            if ( rle_size - src_idx >= (16/8) ) // хватает ли места в исходном буфере на 2 байта пикселя?
            {
                /// мультипликация байтов пикселя
                blue = rle_array[src_idx] & 0b00011111;
                green = ( ( rle_array[src_idx] >> 5 ) | ( rle_array[src_idx + 1] << 3 ) ) & 0b00011111;
                red = ( ( rle_array[src_idx + 1] >> 2 ) & 0b00011111);
                four_bytes.BBGGRR.BB = ( blue << 3 ) | ( blue >> 2 );
                four_bytes.BBGGRR.GG = ( green << 3 ) | ( green >> 2 );
                four_bytes.BBGGRR.RR = ( red << 3 ) | ( red >> 2 );
                fill_with_dword(four_bytes.dword, &dst_array[dst_idx], group_cnt);
                ///
                src_idx += (16/8); // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }
        else // пакет не-rle группы
        {
            ++src_idx; // перестановка на байты пикселя
            not_packed_bytes = group_cnt << 1; // group_cnt*(16/8);
            if ( rle_size - src_idx >= not_packed_bytes ) // хватает ли места в исходном буфере на group_cnt*2 байтов пикселей?
            {
                /// копирование байтов пикселей
                auto w_array = (quint16*)&rle_array[src_idx]; // array of words
                for(qint64 w_idx = 0; w_idx < group_cnt; ++w_idx)
                {
                    blue = (quint8) ( w_array[w_idx] & 0b00000000'00011111 );
                    green = (quint8) ( ( w_array[w_idx] >> 5 ) & 0b00000000'00011111 );
                    red = (quint8) ( ( w_array[w_idx] >> 10 ) & 0b00000000'00011111 );
                    four_bytes.BBGGRR.BB = ( blue << 3 ) | ( blue >> 2 );
                    four_bytes.BBGGRR.GG = ( green << 3 ) | ( green >> 2 );
                    four_bytes.BBGGRR.RR = ( red << 3 ) | ( red >> 2 );
                    *((quint32*)&dst_array[dst_idx + (w_idx << 2)]) = four_bytes.dword; // w_idx*4
                }
                ///
                src_idx += not_packed_bytes; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }

        dst_idx += (group_cnt << 2); // обновляем общий счётчик байтов (он же индекс следующей позиции заполнения в dst_array) : сдвиг влево на 2 = *4

    } while(pix_cnt < total_size_p); // декодировали пикселей столько, сколько должны => конец цикла

    state = FSM_States::DecodedOK;
    return GIA_TgaErr::Success;
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_rle16()
{
    quint8 *rle_array = &src_array[pix_data_offset];
    qint64 rle_size = src_size - pix_data_offset; // rle array size (from pix_data_offset to the end of source file)
    qint64 src_idx = 0; // byte index in rle_array
    qint64 dst_idx = 0; // byte index in dst_array
    qint64 pix_cnt = 0; // decoded pixels counter
    qint64 group_cnt; // group counter for rle or non-rle pixels
    qint64 not_packed_bytes;
    bbggrraa four_bytes;
    quint8 blue, green, red;
    do {
        /// хватает ли места для очередного счётчика группы ?
        if ( rle_size - src_idx < 1 ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TruncDataAbort; } // досрочный выход из цикла : нехватка байтов исходных данных

        group_cnt = (rle_array[src_idx] & 0b01111111) + 1; // счётчик группы всегда кодирует минимум 1 пиксель
        pix_cnt += group_cnt; // обновляем общий счётчик пикселей
        if ( pix_cnt > total_size_p ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TooMuchPixAbort; } // досрочный выход : вылезли за пределы размеров изображения, некорректные rle-данные

        if ( (rle_array[src_idx] >> 7) == 1 ) // пакет rle-группы
        {
            ++src_idx; // перестановка на байты пикселя
            if ( rle_size - src_idx >= (16/8) ) // хватает ли места в исходном буфере на 2 байта пикселя?
            {
                /// мультипликация байтов пикселя
                blue = rle_array[src_idx] & 0b00011111;
                green = ( ( rle_array[src_idx] >> 5 ) | ( rle_array[src_idx + 1] << 3 ) ) & 0b00011111;
                red = ( ( rle_array[src_idx + 1] >> 2 ) & 0b00011111);
                four_bytes.BBGGRR.BB = ( blue << 3 ) | ( blue >> 2 );
                four_bytes.BBGGRR.GG = ( green << 3 ) | ( green >> 2 );
                four_bytes.BBGGRR.RR = ( red << 3 ) | ( red >> 2);
                four_bytes.AA = ( (rle_array[src_idx + 1] & 0b10000000) == 0b10000000 ) ? 0 : 255;
                fill_with_dword(four_bytes.dword, &dst_array[dst_idx], group_cnt);
                ///
                src_idx += 2; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }
        else // пакет не-rle группы
        {
            ++src_idx; // перестановка на байты пикселя
            not_packed_bytes = group_cnt << 1; // group_cnt * 2
            if ( rle_size - src_idx >= not_packed_bytes ) // хватает ли места в исходном буфере на group_cnt*2 байтов пикселей?
            {
                /// копирование байтов пикселей
                auto w_array = (quint16*)&rle_array[src_idx]; // array of words
                for(qint64 w_idx = 0; w_idx < group_cnt; ++w_idx)
                {
                    blue = (quint8) ( w_array[w_idx] & 0b00000000'00011111 );
                    green = (quint8) ( ( w_array[w_idx] >> 5 ) & 0b00000000'00011111 );
                    red = (quint8) ( ( w_array[w_idx] >> 10 ) & 0b00000000'00011111 );
                    four_bytes.BBGGRR.BB = ( blue << 3 ) | ( blue >> 2 );
                    four_bytes.BBGGRR.GG = ( green << 3 ) | ( green >> 2 );
                    four_bytes.BBGGRR.RR = ( red << 3 ) | ( red >> 2 );
                    four_bytes.AA = ( (w_array[w_idx] & 0b10000000'00000000) == 0b10000000'00000000 ) ? 0 : 255;
                    *((quint32*)&dst_array[dst_idx + (w_idx << 2)]) = four_bytes.dword; // w_idx*4
                }
                ///
                src_idx += not_packed_bytes; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }

        dst_idx += (group_cnt << 2); // обновляем общий счётчик байтов (он же индекс следующей позиции заполнения в dst_array) : сдвиг влево на 2 = *4

    } while(pix_cnt < total_size_p); // декодировали пикселей столько, сколько должны => конец цикла

    state = FSM_States::DecodedOK;
    return GIA_TgaErr::Success;
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_rle24()
{
    quint8 *rle_array = &src_array[pix_data_offset];
    qint64 rle_size = src_size - pix_data_offset; // rle array size (from pix_data_offset to the end of source file)
    qint64 src_idx = 0; // byte index in rle_array
    qint64 dst_idx = 0; // byte index in dst_array
    qint64 pix_cnt = 0; // decoded pixels counter
    qint64 group_cnt; // group counter for rle or non-rle pixels
    qint64 not_packed_bytes;
    bbggrraa four_bytes;
    four_bytes.AA = 0xFF;
    do {
        /// хватает ли места для очередного счётчика группы ?
        if ( rle_size - src_idx < 1 ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TruncDataAbort; } // досрочный выход из цикла : нехватка байтов исходных данных

        group_cnt = (rle_array[src_idx] & 0b01111111) + 1; // счётчик группы всегда кодирует минимум 1 пиксель
        pix_cnt += group_cnt; // обновляем общий счётчик пикселей
        if ( pix_cnt > total_size_p ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TooMuchPixAbort; } // досрочный выход : вылезли за пределы размеров изображения, некорректные rle-данные

        if ( (rle_array[src_idx] >> 7) == 1 ) // пакет rle-группы
        {
            ++src_idx; // перестановка на байты пикселя
            if ( rle_size - src_idx >= 3 ) // хватает ли места в исходном буфере на 3 байта пикселя?
            {
                /// мультипликация байтов пикселя
                four_bytes.BBGGRR = *((triplet*)&rle_array[src_idx]);
                fill_with_dword(four_bytes.dword, &dst_array[dst_idx], group_cnt);
                ///
                src_idx += (24/8); // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }
        else // пакет не-rle группы
        {
            ++src_idx; // перестановка на байты пикселя
            not_packed_bytes = group_cnt * 3;
            if ( rle_size - src_idx >= not_packed_bytes ) // хватает ли места в исходном буфере на group_cnt*3 байтов пикселей?
            {
                /// копирование байтов пикселей
                for(qint64 trp_idx = 0; trp_idx < group_cnt; ++trp_idx)
                {
                    four_bytes.BBGGRR = ((triplet*)&rle_array[src_idx])[trp_idx];
                    *((quint32*)&dst_array[dst_idx + (trp_idx << 2)]) = four_bytes.dword; // trp_idx*4
                }
                ///
                src_idx += not_packed_bytes; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }

        dst_idx += (group_cnt << 2); // обновляем общий счётчик байтов (он же индекс следующей позиции заполнения в dst_array) : сдвиг влево на 2 = *4

    } while(pix_cnt < total_size_p); // декодировали пикселей столько, сколько должны => конец цикла

    state = FSM_States::DecodedOK;
    return GIA_TgaErr::Success;
}

GIA_TgaErr GIA_TgaDecoder::decode_tc_rle32()
{
    quint8 *rle_array = &src_array[pix_data_offset];
    qint64 rle_size = src_size - pix_data_offset; // rle array size (from pix_data_offset to the end of source file)
    qint64 src_idx = 0; // byte index in rle_array
    qint64 dst_idx = 0; // byte index in dst_array
    qint64 pix_cnt = 0; // decoded pixels counter
    qint64 group_cnt; // group counter for rle or non-rle pixels
    qint64 not_packed_bytes;
    do {
        /// хватает ли места для очередного счётчика группы ?
        if ( rle_size - src_idx < 1 ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TruncDataAbort; } // досрочный выход из цикла : нехватка байтов исходных данных

        group_cnt = (rle_array[src_idx] & 0b01111111) + 1; // счётчик группы всегда кодирует минимум 1 пиксель
        pix_cnt += group_cnt; // обновляем общий счётчик пикселей
        if ( pix_cnt > total_size_p ) { state = FSM_States::DecodingAbort; return GIA_TgaErr::TooMuchPixAbort; } // досрочный выход : вылезли за пределы размеров изображения, некорректные rle-данные

        if ( (rle_array[src_idx] >> 7) == 1 ) // пакет rle-группы
        {
            ++src_idx; // перестановка на байты пикселя
            if ( rle_size - src_idx >= 4 ) // хватает ли места в исходном буфере на 4 байта пикселя?
            {
                /// мультипликация байтов пикселя
                fill_with_dword(*((quint32*)&rle_array[src_idx]), &dst_array[dst_idx], group_cnt);
                ///
                src_idx += 4; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }
        else // пакет не-rle группы
        {
            ++src_idx; // перестановка на байты пикселя
            not_packed_bytes = group_cnt << 2; // group_cnt * 4
            if ( rle_size - src_idx >= not_packed_bytes ) // хватает ли места в исходном буфере на group_cnt*4 байтов пикселей?
            {
                /// копирование байтов пикселей
                std::memcpy(&dst_array[dst_idx], &rle_array[src_idx], not_packed_bytes);
                ///
                src_idx += not_packed_bytes; // перестановка на следующий счётчик группы
            }
            else // не хватает места на байты пикселя
            {
                state = FSM_States::DecodingAbort;
                return GIA_TgaErr::TruncDataAbort; // досрочный выход из цикла : нехватка исходных байтов данных
            }
        }

        dst_idx += (group_cnt << 2); // обновляем общий счётчик байтов (он же индекс следующей позиции для заполнения в dst_array) : сдвиг влево на 2 = *4

    } while(pix_cnt < total_size_p); // декодировали пикселей столько, сколько должны => конец цикла

    state = FSM_States::DecodedOK;
    return GIA_TgaErr::Success;
}

}
