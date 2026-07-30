#include "PluginSlotCore.h"

namespace sg
{

juce::String stateBlobToBase64 (const juce::MemoryBlock& blob)
{
    return juce::Base64::toBase64 (blob.getData(), blob.getSize());
}

bool base64ToStateBlob (const juce::String& base64Text, juce::MemoryBlock& outBlob)
{
    if (base64Text.isEmpty())
    {
        outBlob.reset();
        return true;
    }

    juce::MemoryOutputStream decoded;
    if (! juce::Base64::convertFromBase64 (decoded, base64Text))
        return false;

    outBlob = decoded.getMemoryBlock();
    return true;
}

} // namespace sg
