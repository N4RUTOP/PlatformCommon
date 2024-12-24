/**
 *   Http 2.0 Frame implement from frame.py
 * 
 *   Created by lihuanqian 12/17/2024
 * 
 *   Copyright (c) lihuanqian. All rights reserved.
 * 
 */

#pragma once

#include <vector>
#include <map>

static constexpr uint32_t HTTP2_HEAD_SIZE = 9;

static const std::vector<uint8_t> HTTP2_MAGIC = {
    'P', 'R', 'I', ' ', '*', ' ', 'H', 'T', 'T', 'P', '/', '2', '.', '0', '\r', '\n',
    '\r', '\n', 'S', 'M', '\r', '\n', '\r', '\n'
};

class Http2Frame
{
public:
    Http2Frame(uint32_t streamId, uint8_t flags, uint8_t type);
	virtual ~Http2Frame() {}

	/** serialize all data */
	virtual std::vector<uint8_t> serialize();

    /** serialize head data */
	virtual std::vector<uint8_t> serializeHead();

    /** serialize body data */
	virtual std::vector<uint8_t> serializeBody() = 0;

protected:
	uint32_t m_streamId;
	uint8_t m_flags;
	uint8_t m_type;
	uint32_t m_bodyLen;
};

/**
 * @brief Setting Frame
 */
class Http2SettingsFrame : public Http2Frame
{
public:
    // The flags defined for SETTINGS frames.
    static constexpr uint8_t FLAG_ACK = 0x01;

    // The type byte defined for SETTINGS frames.
    static constexpr uint8_t TYPE = 0x04;

    // Stream association (no stream in SETTINGS frames)
    static constexpr uint32_t STREAM_ASSOC_NO_STREAM = 0;

    // The known settings
    enum SettingOption : uint16_t
    {
        HEADER_TABLE_SIZE = 0x01,
        ENABLE_PUSH = 0x02,
        MAX_CONCURRENT_STREAMS = 0x03,
        INITIAL_WINDOW_SIZE = 0x04,
        MAX_FRAME_SIZE = 0x05,
        MAX_HEADER_LIST_SIZE = 0x06,
        ENABLE_CONNECT_PROTOCOL = 0x08
    };
    using SettingsMap = std::map<SettingOption, uint32_t>;

    Http2SettingsFrame(uint8_t flags = 0, uint32_t streamID = STREAM_ASSOC_NO_STREAM, const SettingsMap& settings = {});

public:
	std::vector<uint8_t> serializeBody() override;
    SettingsMap m_settings;
};

/**
 * @brief Window Update Frame
 */
class Http2WindowUpdateFrame : public Http2Frame
{
public:
    static constexpr uint8_t TYPE = 0x08;

    Http2WindowUpdateFrame(uint32_t streamId, uint32_t windowIncrement);

    std::vector<uint8_t> serializeBody() override;

private:
    uint32_t m_windowIncrement;
};

/**
 * @brief Headers Frame
 */
class Http2HeadersFrame : public Http2Frame
{
public:
    static constexpr uint8_t TYPE = 0x01;

    // Flags
    enum Flags : uint8_t
    {
        END_STREAM = 0x01,
        END_HEADERS = 0x04,
        PADDED = 0x08,
        PRIORITY = 0x20
    };

    Http2HeadersFrame(
        uint32_t streamId,
        const std::vector<uint8_t>& headerBlock,
        Flags flags,
        uint8_t padLength = 0,
        uint32_t dependsOn = 0,
        uint8_t weight = 0);

    std::vector<uint8_t> serializeBody() override;

private:
    std::vector<uint8_t> m_headerBlock;
    uint8_t m_padLength;
    uint32_t m_dependsOn;
    uint8_t m_weight;
};

/**
 * @brief Data Frame
 */
class Http2DataFrame : public Http2Frame
{
public:
    static constexpr uint8_t TYPE = 0x00;

    Http2DataFrame(uint32_t streamId, const std::vector<uint8_t>& data, uint8_t padLength = 0);

    std::vector<uint8_t> serializeBody() override;

private:
    std::vector<uint8_t> m_data;
    uint8_t m_padLength;
};

/**
 *  -------------  --------------
 * | Size 3byte  ||  Type 1byte  |
 *  -------------  --------------
 *  ------------- 
 * | Flags 1byte |
 *  ------------- 
 *  -----------------------------
 * |       Stream id 4byte       |
 *  -----------------------------
 *  -----------------------------
 * |            Data             |
 *  -----------------------------
 */
class Http2FrameHeadParser 
{
public:
    enum FrameType
    {
        None,
        HeadFrame,
        SettingFrame,
        DataFrame,
        WindowsUpdateFrame,
        UnkownFrame
    };

    Http2FrameHeadParser(const std::vector<uint8_t>& frameHead);
    void setFrameHead(const std::vector<uint8_t>& frameHead);

    /** get frame type */
    FrameType getFrameType();

    /** get data size */
    uint32_t getDataSize();

    /** get flags */
    uint8_t getFlags();

    /** get stream id */
    uint32_t getStreamId();

private:
    std::vector<uint8_t> m_data;
};