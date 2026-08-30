bool PreviewRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                 unsigned int rt_width, unsigned int rt_height)
{
    m_Device = device;
    m_Ctx = ctx;
    m_Width = rt_width;
    m_Height = rt_height;

    if (!CreateRenderTarget(rt_width, rt_height)) return false;
    if (!CreateShaders()) return false;

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    m_Device->CreateRasterizerState(&rd, &m_RS);

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    m_Device->CreateDepthStencilState(&dd, &m_DSState);

    D3D11_DEPTH_STENCIL_DESC dd2 = dd;
    dd2.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    m_Device->CreateDepthStencilState(&dd2, &m_DSNoWrite);

    D3D11_BLEND_DESC bld{};
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_Device->CreateBlendState(&bld, &m_BlendOpaque);

    D3D11_BLEND_DESC abl{};
    abl.RenderTarget[0].BlendEnable = TRUE;
    abl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    abl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    abl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    abl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    abl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    abl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    abl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_Device->CreateBlendState(&abl, &m_BlendAlpha);

    m_Ready = true;
    return true;
}

bool PreviewRenderer::CreateRenderTarget(unsigned int w, unsigned int h)
{
	m_SampleCount = 1;
	UINT quality = 0;
	if (SUCCEEDED(m_Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality)) && quality > 0)
		m_SampleCount = 4;

	else if (SUCCEEDED(m_Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 2, &quality)) && quality > 0)
		m_SampleCount = 2;

	D3D11_TEXTURE2D_DESC td{};
	td.Width = w;
	td.Height = h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = m_SampleCount;
	td.SampleDesc.Quality = 0;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET;
	if (FAILED(m_Device->CreateTexture2D(&td, nullptr, &m_RTTex)))
		return false;
	if (FAILED(m_Device->CreateRenderTargetView(m_RTTex, nullptr, &m_RTV)))
		return false;

	D3D11_TEXTURE2D_DESC rd = td;
	rd.SampleDesc.Count = 1;
	rd.SampleDesc.Quality = 0;
	rd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	if (FAILED(m_Device->CreateTexture2D(&rd, nullptr, &m_ResolveTex)))
		return false;
	if (FAILED(m_Device->CreateShaderResourceView(m_ResolveTex, nullptr, &m_SRV)))
		return false;

	D3D11_TEXTURE2D_DESC dd{};
	dd.Width = w;
	dd.Height = h;
	dd.MipLevels = 1;
	dd.ArraySize = 1;
	dd.Format = DXGI_FORMAT_D32_FLOAT;
	dd.SampleDesc.Count = m_SampleCount;
	dd.SampleDesc.Quality = 0;
	dd.Usage = D3D11_USAGE_DEFAULT;
	dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(m_Device->CreateTexture2D(&dd, nullptr, &m_DepthTex)))
		return false;
	return SUCCEEDED(m_Device->CreateDepthStencilView(m_DepthTex, nullptr, &m_DSV));
}

bool PreviewRenderer::CreateShaders()
{
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    if (!CompileShader(s_VS, "main", "vs_4_0", &vsBlob)) return false;
    if (!CompileShader(s_PS, "main", "ps_4_0", &psBlob)) { vsBlob->Release(); return false; }

    HRESULT hr = m_Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_VS);
    if (SUCCEEDED(hr))
        hr = m_Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_PS);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (SUCCEEDED(hr))
        hr = m_Device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_IL);

    vsBlob->Release();
    psBlob->Release();
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 160;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(m_Device->CreateBuffer(&cbd, nullptr, &m_CB));
}

