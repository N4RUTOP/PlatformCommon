#include "Http2Frame.h"



/** Http2Frame */
Http2Frame::Http2Frame(uint32_t streamId, uint8_t flags, uint8_t type):
    m_streamId(streamId),
    m_flags(flags),
    m_type(type),
    m_bodyLen(0)
{
}

std::vector<uint8_t> Http2Frame::serialize()
{
    std::vector<uint8_t> body = serializeBody();
    m_bodyLen = (uint32_t)body.size();
    std::vector<uint8_t> head = serializeHead();
    head.insert(head.end(), body.begin(), body.end());
    return head;
}

std::vector<uint8_t> Http2Frame::serializeHead()
{
    std::vector<uint8_t> header;

    // Length spread 8 bit of 24
    header.push_back((m_bodyLen >> 16) & 0xFF); 
    header.push_back((m_bodyLen >> 8) & 0xFF);  
    header.push_back(m_bodyLen & 0xFF);         

    header.push_back(m_type);  // type 
    header.push_back(m_flags); // flags 

    header.push_back((m_streamId >> 24) & 0xFF); // MSB
    header.push_back((m_streamId >> 16) & 0xFF);
    header.push_back((m_streamId >> 8) & 0xFF);
    header.push_back(m_streamId & 0xFF); // LSB

    return header;
}

/** Http2SettingsFrame */
Http2SettingsFrame::Http2SettingsFrame(uint8_t flags, uint32_t streamID, const SettingsMap& settings):
    Http2Frame(streamID, flags, TYPE),
    m_settings(settings)
{
}

std::vector<uint8_t> Http2SettingsFrame::serializeBody()
{
    std::vector<uint8_t> serialized;
    for (const auto& entry : m_settings) {
        // Each setting consists of a 1-byte setting ID and a 4-byte setting value
        serialized.push_back((entry.first >> 8) & 0xFF);
        serialized.push_back(entry.first & 0xFF);
        serialized.push_back((entry.second >> 24) & 0xFF);  // MSB
        serialized.push_back((entry.second >> 16) & 0xFF);
        serialized.push_back((entry.second >> 8) & 0xFF);
        serialized.push_back(entry.second & 0xFF);  // LSB
    }
    return serialized;
}

/** Http2WindowUpdateFrame */
Http2WindowUpdateFrame::Http2WindowUpdateFrame(uint32_t streamId, uint32_t windowIncrement)
    : Http2Frame(streamId, 0, TYPE), m_windowIncrement(windowIncrement)
{
}

std::vector<uint8_t> Http2WindowUpdateFrame::serializeBody()
{
    std::vector<uint8_t> body;
    body.push_back((m_windowIncrement >> 24) & 0xFF);
    body.push_back((m_windowIncrement >> 16) & 0xFF);
    body.push_back((m_windowIncrement >> 8) & 0xFF);
    body.push_back(m_windowIncrement & 0xFF);
    return body;
}

/** Http2HeadersFrame */
Http2HeadersFrame::Http2HeadersFrame(
    uint32_t streamId,
    const std::vector<uint8_t>& headerBlock,
    Flags flags,
    uint8_t padLength,
    uint32_t dependsOn,
    uint8_t weight)
    : Http2Frame(streamId, (uint8_t)flags, TYPE),
    m_headerBlock(headerBlock),
    m_padLength(padLength),
    m_dependsOn(dependsOn),
    m_weight(weight)
{
    
}

std::vector<uint8_t> Http2HeadersFrame::serializeBody()
{
    std::vector<uint8_t> body;
    if (m_padLength > 0 && m_flags & PADDED) {
        body.push_back(m_padLength);
    }
    if (m_flags & PRIORITY) {
        body.push_back((m_dependsOn >> 24) & 0xFF);
        body.push_back((m_dependsOn >> 16) & 0xFF);
        body.push_back((m_dependsOn >> 8) & 0xFF);
        body.push_back(m_dependsOn & 0xFF);
        body.push_back(m_weight);
    }
    body.insert(body.end(), m_headerBlock.begin(), m_headerBlock.end());
    body.insert(body.end(), m_padLength, 0); // Add padding
    return body;
}

/** Http2DataFrame */
Http2DataFrame::Http2DataFrame(uint32_t streamId, const std::vector<uint8_t>& data, uint8_t padLength)
    : Http2Frame(streamId, padLength > 0 ? 0x08 : 0x00, TYPE), m_data(data), m_padLength(padLength)
{
}

std::vector<uint8_t> Http2DataFrame::serializeBody()
{
    std::vector<uint8_t> body;
    if (m_padLength > 0) {
        body.push_back(m_padLength);
    }
    body.insert(body.end(), m_data.begin(), m_data.end());
    body.insert(body.end(), m_padLength, 0); // Add padding
    return body;
}

Http2FrameHeadParser::Http2FrameHeadParser(const std::vector<uint8_t>& data):
    m_data(data)
{
}

void Http2FrameHeadParser::setFrameHead(const std::vector<uint8_t>& frameHead)
{
    m_data = frameHead;
}

Http2FrameHeadParser::FrameType Http2FrameHeadParser::getFrameType()
{
    if (m_data.size() < 9) {
        return Http2FrameHeadParser::None;
    }
    uint8_t type = m_data[3];
    switch (type)
    {
    case Http2SettingsFrame::TYPE:
        return Http2FrameHeadParser::SettingFrame;

    case Http2DataFrame::TYPE:
        return Http2FrameHeadParser::DataFrame;

    case Http2HeadersFrame::TYPE:
        return Http2FrameHeadParser::HeadFrame;

    case Http2WindowUpdateFrame::TYPE:
        return Http2FrameHeadParser::WindowsUpdateFrame;

    default:
        break;
    }

    return Http2FrameHeadParser::UnkownFrame;
}

uint32_t Http2FrameHeadParser::getDataSize()
{
    if (m_data.size() < 9) {
        return 0;
    }
    return (static_cast<uint32_t>(m_data[0]) << 16) |
           (static_cast<uint32_t>(m_data[1]) << 8) |
           static_cast<uint32_t>(m_data[2]);
}

uint8_t Http2FrameHeadParser::getFlags()
{
    if (m_data.size() < 9) {
        return 0;
    }
    return m_data[4];
}

uint32_t Http2FrameHeadParser::getStreamId()
{
    if (m_data.size() < 9) {
        return 0;
    }
    return (static_cast<uint32_t>(m_data[5]) << 24) |
           (static_cast<uint32_t>(m_data[6]) << 16) |
           (static_cast<uint32_t>(m_data[7]) << 8) |
           static_cast<uint32_t>(m_data[8]);
}
