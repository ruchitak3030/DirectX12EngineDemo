#include "HeaderHelper.h"
#include "Game.h"
#include "DDSTextureLoader.h"

Game::Game(HINSTANCE hInstance) :
	DXSample(
        hInstance,
        "DirectX Game",
        1280,
        720,
        true
    ),
	m_pCbvDataBegin(nullptr),
	m_constantBufferData{}
{
}

Game::~Game()
{
    delete sphereEntity;
    delete camera;
}

void Game::Init()
{
    LoadShaders();
    CreateMatrices();
    CreateBasicGeometry();
    CreateConstantBuffers();
    LoadTextures();
    CreateRootSignature();
    CreatePSO();
}

void Game::Update(float deltaTime, float totalTime)
{
    camera->Update(deltaTime);
    sphereEntity->UpdateWorldMatrix();

    // Update the constant buffers
    UpdateSceneConstantBuffer(deltaTime);
    UpdateLightConstantBuffer(deltaTime);
    UpdateMaterialConstantBuffer();
}

void Game::Render(float deltaTime, float totalTime)
{
    PopulateCommandList();

    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame
    ThrowIfFailed(m_swapChain->Present(1, 0));

    WaitForPreviousFrame();
}

void Game::Destroy()
{
    WaitForPreviousFrame();
    CloseHandle(m_fenceEvent);
}

void Game::OnResize()
{
    DXSample::OnResize();

    if (camera)
    {
        camera->UpdateProjectionMatrix(m_aspectRatio);
    }
}

void Game::LoadShaders()
{

    // Compile vertex shader shader
    //ID3DBlob* vsBlob = nullptr;
    HRESULT hr = CompileShader(GetAssetFullPath(L"//Shaders//VertexShader.hlsl").c_str(), 
        "main",         
        "vs_5_0", 
        &m_vertexShader);

    if (FAILED(hr))
    {
        printf("Failed compiling vertex shader %08X\n", hr);
        //return -1;
    }


    // Compile pixel shader shader
    //ID3DBlob* psBlob = nullptr;
    hr = CompileShader(GetAssetFullPath(L"//Shaders//PixelShader.hlsl").c_str(),
        "main", 
        "ps_5_0", 
        &m_pixelShader);

    if (FAILED(hr))
    {
        printf("Failed compiling pixel shader %08X\n", hr);
        //return -1;
    }

    
}

void Game::CreateMatrices()
{
    camera = new Camera(m_cameraPos.x, m_cameraPos.y, m_cameraPos.z);
    camera->UpdateProjectionMatrix(m_aspectRatio);
}

void Game::CreateBasicGeometry()
{
    char sphereAsset[128];
    int ret = wcstombs(sphereAsset, GetAssetFullPath(L"//Assets//nanosuit//nanosuit.obj").c_str(), sizeof(sphereAsset));

    sphereEntity = new GameEntity(sphereAsset, XMFLOAT3(0.0f, 50.0f, 0.0f), m_device);
    sphereEntity->SetScale(1.0f, 1.0f, 1.0f);
}

void Game::CreateConstantBuffers()
{
    // Create the constant buffer
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(m_AlignedSceneCBSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_constantBuffer)));

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(m_AlignedLightCBSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_lightCB)));

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(m_AlignedMaterialCBSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_materialCB)));

    // Describe and create constant buffer view
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc[3] = {};
    cbvDesc[0].BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
    cbvDesc[0].SizeInBytes = m_AlignedSceneCBSize;
    cbvDesc[1].BufferLocation = m_lightCB->GetGPUVirtualAddress();
    cbvDesc[1].SizeInBytes = m_AlignedLightCBSize;
    cbvDesc[2].BufferLocation = m_materialCB->GetGPUVirtualAddress();
    cbvDesc[2].SizeInBytes = m_AlignedMaterialCBSize;
        
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle0(m_cbvHeap->GetCPUDescriptorHandleForHeapStart(), 0, 0);
    m_device->CreateConstantBufferView(&cbvDesc[0], cbvHandle0);

    cbvHandle0.Offset(m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    m_device->CreateConstantBufferView(&cbvDesc[1], cbvHandle0);

    cbvHandle0.Offset(m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    m_device->CreateConstantBufferView(&cbvDesc[2], cbvHandle0);

    // Map and initialize the constant buffer
    ThrowIfFailed(m_constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pCbvDataBegin)));
    memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));

    ThrowIfFailed(m_lightCB->Map(0, nullptr, reinterpret_cast<void**>(&m_plightCbvDataBegin)));
    memcpy(m_plightCbvDataBegin, &m_lightCBData, sizeof(m_lightCBData));

    ThrowIfFailed(m_materialCB->Map(0, nullptr, reinterpret_cast<void**>(&m_pmaterialCbvDataBegin)));
    memcpy(m_pmaterialCbvDataBegin, &m_materialCBData, sizeof(m_materialCBData));
}

void Game::LoadTextures()
{
    
    std::unique_ptr<uint8_t[]> ddsData;
    ThrowIfFailed(LoadDDSTextureFromFile(m_device.Get(),
        GetAssetFullPath(L"//Textures//stone.dds").c_str(),
        m_textureBuffer.ReleaseAndGetAddressOf(),
        ddsData,
        m_textureSubResourceData));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_textureBuffer.Get(), 0, static_cast<UINT>(m_textureSubResourceData.size()));

    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(m_textureUploadBuffer.GetAddressOf())));

    UpdateSubresources(m_commandList.Get(), m_textureBuffer.Get(), m_textureUploadBuffer.Get(), 0, 0, static_cast<UINT>(m_textureSubResourceData.size()), m_textureSubResourceData.data());
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_textureBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // Get the pointer to the start of the heap
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_cbvHeap->GetCPUDescriptorHandleForHeapStart() , 3, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

    // Create the descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = m_textureBuffer->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = m_textureBuffer->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.Texture2D.MostDetailedMip = 0;
    m_device->CreateShaderResourceView(m_textureBuffer.Get(), &srvDesc, srvHandle);

    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    WaitForPreviousFrame();

    // Sampler 
    // Create Sampler Heap
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 1;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap)));

    // Create Sampler Descriptor
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    m_device->CreateSampler(&samplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
        
}

void Game::CreateMaterials()
{
}

void Game::CreateRootSignature()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Create single descriptor table of CBVs.
    CD3DX12_DESCRIPTOR_RANGE ranges[3] = {};
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 3, 0, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].InitAsDescriptorTable(2, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[1].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);


    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    // Create a root signature with a single slot which points to the root parameter which consists of 2 descriptor tables as the range.
    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
}

void Game::CreatePSO()
{

    // Input Layout
    D3D12_INPUT_ELEMENT_DESC inputElementDesc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // Describe and create graphics pipeline object (PSO)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDesc, _countof(inputElementDesc) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

}

void Game::PopulateCommandList()
{
    // Reset Command allocators and command list
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    // Set necessary states
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get(), m_samplerHeap.Get()};
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    m_commandList->SetGraphicsRootDescriptorTable(0, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRootDescriptorTable(1, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate the back buffer will be used as render target
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

    m_commandList->ClearRenderTargetView(rtvHandle, Colors::Black, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH , 1.0f, 0, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //m_commandList->IASetVertexBuffers(0, 1, &sphereEntity->GetMesh()->GetVertexBufferView());
    //m_commandList->IASetIndexBuffer(&sphereEntity->GetMesh()->GetIndexBufferView());

    // TODO: Consider the case where a GameEntity consists of multiple meshes
    sphereEntity->Draw(m_commandList);

   // m_commandList->DrawIndexedInstanced(sphereEntity->GetMesh()->GetIndexCount(), 1, 0, 0, 0);

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(m_commandList->Close());
}

void Game::UpdateSceneConstantBuffer(float deltaTime)
{
    //Update constant buffer with latest world-view-proj matrix.
    XMStoreFloat4x4(&m_constantBufferData.worldMatrix, XMLoadFloat4x4(sphereEntity->GetWorldMatrix()));
    XMStoreFloat4x4(&m_constantBufferData.viewMatrix, XMLoadFloat4x4(&camera->GetViewMatrix()));
    XMStoreFloat4x4(&m_constantBufferData.projMatrix, XMLoadFloat4x4(&camera->GetProjectionMatrix()));
    memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));
}

void Game::UpdateLightConstantBuffer(float deltaTime)
{
    //XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    //float FalloffStart = 1.0f;                              // point/spot light only
    //XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };    // directional/spot light only
    //float FalloffEnd = 10.0f;                               // point/spot light only
    //XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };      // point/spot light only
    //float SpotPower = 64.0f;

    m_lightCBData.AmbientLight = XMFLOAT4( 0.0f, 0.0f, 0.0f, 1.0f );
    m_lightCBData.EyePos = m_cameraPos;
    m_lightCBData.pad = 0;

    // 2 directional lights
    m_lightCBData.lights[0].Direction = XMFLOAT3(1.0f, 0.0f, 0.0f);
    m_lightCBData.lights[0].Strength = XMFLOAT3(0.4f, 0.4f, 0.4f);

    // 1 point light
    m_lightCBData.lights[1].Strength = XMFLOAT3(0.83f, 0.10f, 0.12f);
    m_lightCBData.lights[1].FalloffStart = 1.0f;
    m_lightCBData.lights[1].Position = XMFLOAT3(-2.0f, 8.0f, 0.0f);
    m_lightCBData.lights[1].FalloffEnd = 10.0f;

    // 1 Spot light
    m_lightCBData.lights[2].Strength = XMFLOAT3(0.89f, 0.51f, 0.09f);
    m_lightCBData.lights[2].FalloffStart = 1.0f;
    m_lightCBData.lights[2].Direction = XMFLOAT3(0.0f, 8.0f, -1.0f);
    m_lightCBData.lights[2].FalloffEnd = 10.0f;
    m_lightCBData.lights[2].Position = XMFLOAT3(0.0f, 9.0f, -3.0f);
    m_lightCBData.lights[2].SpotPower = 1.0f;

    memcpy(m_plightCbvDataBegin, &m_lightCBData, sizeof(m_lightCBData));
}

void Game::UpdateMaterialConstantBuffer()
{
    m_materialCBData.DiffuseAlbedo = XMFLOAT4(DirectX::Colors::DarkGray);
    m_materialCBData.FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    m_materialCBData.Roughness = 0.8f;

    memcpy(m_pmaterialCbvDataBegin, &m_materialCBData, sizeof(m_materialCBData));
}

HRESULT Game::CompileShader(LPCWSTR srcFile, LPCSTR entryPoint, LPCSTR profile, ID3DBlob** blob)
{
    if (!srcFile || !entryPoint || !profile || !blob)
        return E_INVALIDARG;

    *blob = nullptr;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
    flags |= D3DCOMPILE_DEBUG;
#endif

    const D3D_SHADER_MACRO defines[] =
    {
        "EXAMPLE_DEFINE", "1",
        NULL, NULL
    };

    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompileFromFile(srcFile, defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, profile,
        flags, 0, &shaderBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }

        if (shaderBlob)
            shaderBlob->Release();

        return hr;
    }

    *blob = shaderBlob;

    return hr;
}


#pragma region MouseInput
void Game::OnMouseDown(WPARAM buttonState, int x, int y)
{
    m_prevMousePos.x = x;
    m_prevMousePos.y = y;

    SetCapture(m_hWnd);
}

void Game::OnMouseUp(WPARAM buttonState, int x, int y)
{
    ReleaseCapture();
}

void Game::OnMouseMove(WPARAM buttonState, int x, int y)
{
    if (buttonState & 0x0001)
    {
        float xDiff = (x - m_prevMousePos.x) * 0.005f;
        float yDiff = (y - m_prevMousePos.y) * 0.005f;
        camera->Rotate(yDiff, xDiff);

    }

    m_prevMousePos.x = x;
    m_prevMousePos.y = y;
}

void Game::OnMouseWheel(WPARAM wheelData, int x, int y)
{
}

#pragma endregion


