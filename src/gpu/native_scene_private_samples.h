#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstring>
#include <cstdint>

struct PrivatePassedSamples {
    // Commands recorded by RecordedList retain raw pointers. The owning frame
    // holds both COM references until its submission fence retires.
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    bool requested=false;
    bool resolved=false;
    static constexpr UINT64 kBytes=2*sizeof(UINT64);

    bool Allocate(ID3D12Device* device) {
        if (!device || !requested || heap || readback) return false;
        D3D12_QUERY_HEAP_DESC q{};
        q.Type=D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        q.Count=2;
        Microsoft::WRL::ComPtr<ID3D12QueryHeap> local;
        if (FAILED(device->CreateQueryHeap(&q,IID_PPV_ARGS(&local)))) return false;
        D3D12_HEAP_PROPERTIES props{};
        props.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width=kBytes;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)))) return false;
        heap.Swap(local);
        return true;
    }

    bool Read(UINT64 fence, UINT64 completed, UINT64 (&counts)[2]) const {
        if (!resolved || !readback || !fence || completed==UINT64_MAX || completed<fence) return false;
        const D3D12_RANGE range{0,SIZE_T(kBytes)}, none{0,0};
        void* data=nullptr;
        if (FAILED(readback->Map(0,&range,&data))) return false;
        if (data) std::memcpy(counts,data,SIZE_T(kBytes));
        readback->Unmap(0,&none);
        return data!=nullptr;
    }

    static const char* Classify(bool samplesReadable, UINT64 samples, bool planesReadable, bool changed) {
        if (!samplesReadable || !planesReadable) return "unavailable";
        if (!samples) return changed ? "zero_samples_changed" : "no_passing_samples";
        return changed ? "passed_changed" : "passed_unchanged";
    }
};
