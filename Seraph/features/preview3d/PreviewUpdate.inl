void PreviewRenderer::AddRotationDelta(float dyaw, float dpitch)
{
	m_ManualYaw += dyaw;
	m_ManualPitch += dpitch;
	if (m_ManualPitch > 1.2f) m_ManualPitch = 1.2f;
	if (m_ManualPitch < -1.2f) m_ManualPitch = -1.2f;
	m_SceneDirty = true;
}

void PreviewRenderer::AddZoom(float delta)
{
	m_Zoom += delta;
	if (m_Zoom < 0.45f) m_Zoom = 0.45f;
	if (m_Zoom > 2.8f) m_Zoom = 2.8f;
	m_SceneDirty = true;
}

float PreviewRenderer::WingAlpha() const
{
	// fade in, hold, out, пауза
	float fade_in = 1.15f;
	float hold = 1.60f;
	float fade_out = 1.15f;
	float pause = 0.90f;
	float period = fade_in + hold + fade_out + pause;

	float t = std::fmod(m_WingAnimTime, period);
	if (t < 0.f)
		t += period;

	auto smooth = [](float x)
	{
		if (x < 0.f) x = 0.f;
		if (x > 1.f) x = 1.f;
		return x * x * (3.f - 2.f * x);
	};

	if (t < fade_in)
		return smooth(t / fade_in);
	t -= fade_in;

	if (t < hold)
		return 1.f;
	t -= hold;

	if (t < fade_out)
		return 1.f - smooth(t / fade_out);

	(void)pause;
	return 0.f;
}

void PreviewRenderer::Update(float dt)
{
	if (!m_Ready || !m_VB)
		return;

	if (m_SpinPauseRemaining > 0.0f)
		m_SpinPauseRemaining -= dt;

	if (m_AutoSpin && m_SpinPauseRemaining <= 0.0f)
	{
		m_SpinAccum += dt;
		if (m_SpinAccum >= 1.0f / 20.0f)
		{
			m_Angle += m_SpinAccum * 0.35f;
			m_SpinAccum = 0.0f;
			m_SceneDirty = true;
		}
	}

	else
	{
		m_SpinAccum = 0.0f;
	}

	if (m_WingVertCount > 0)
	{
		m_WingAnimTime += dt;
		m_SceneDirty = true;
	}

	if (!m_SceneDirty)
		return;
	m_SceneDirty = false;

	float totalYaw = m_Angle + m_ManualYaw + m_ModelYawOffset;
	float totalPitch = m_ManualPitch + m_ModelPitchOffset;
	float camDist = 2.35f / m_Zoom;

	XMMATRIX world = XMMatrixTranslation(-m_FrameCenter[0], -m_FrameCenter[1], -m_FrameCenter[2])
	               * XMMatrixScaling(m_FrameScale, m_FrameScale, m_FrameScale)
	               * XMMatrixRotationX(totalPitch)
	               * XMMatrixRotationY(totalYaw);

	float aspect = (float)m_Width / (float)m_Height;
	XMMATRIX view = XMMatrixLookAtLH(
		XMVectorSet(0.0f, 0.02f, -camDist, 1.0f),
		XMVectorSet(0.0f, 0.02f, 0.0f, 1.0f),
		XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(40.0f), aspect, 0.01f, 100.0f);

	XMMATRIX cpuMVP = world * view * proj;
	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(m_LastMVP), cpuMVP);
	m_LastMVPValid = true;
	m_CachedBoundsValid = false;

	auto upload_cb = [&](float opacity)
	{
		XMMATRIX gpuMVP = XMMatrixTranspose(cpuMVP);
		XMMATRIX gpuWorld = XMMatrixTranspose(world);
		D3D11_MAPPED_SUBRESOURCE ms{};
		if (SUCCEEDED(m_Ctx->Map(m_CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
		{
			float* dst = (float*)ms.pData;
			memcpy(dst, &gpuMVP, 64);
			memcpy(dst + 16, &gpuWorld, 64);
			dst[32] = opacity;
			dst[33] = 0.0f;
			dst[34] = 0.0f;
			dst[35] = 0.0f;
			m_Ctx->Unmap(m_CB, 0);
		}
	};

    ID3D11RenderTargetView* prevRTV = nullptr;
    ID3D11DepthStencilView* prevDSV = nullptr;
    ID3D11RasterizerState* prevRS = nullptr;
    ID3D11DepthStencilState* prevDS = nullptr;
    UINT prevDSRef = 0;
    float prevBF[4]{}; UINT prevSM = 0;
    ID3D11BlendState* prevBS = nullptr;
    UINT numVP = 1; D3D11_VIEWPORT prevVP{};

    m_Ctx->OMGetRenderTargets(1, &prevRTV, &prevDSV);
    m_Ctx->RSGetState(&prevRS);
    m_Ctx->OMGetDepthStencilState(&prevDS, &prevDSRef);
    m_Ctx->OMGetBlendState(&prevBS, prevBF, &prevSM);
    m_Ctx->RSGetViewports(&numVP, &prevVP);

    m_Ctx->OMSetRenderTargets(1, &m_RTV, m_DSV);
    const float clear_bg[4] = {
        colors::child_fill.x,
        colors::child_fill.y,
        colors::child_fill.z,
        colors::child_fill.w
    };
    m_Ctx->ClearRenderTargetView(m_RTV, clear_bg);
    m_Ctx->ClearDepthStencilView(m_DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp{};
    vp.Width = (float)m_Width; vp.Height = (float)m_Height; vp.MaxDepth = 1.0f;
    m_Ctx->RSSetViewports(1, &vp);
    m_Ctx->RSSetState(m_RS);

    UINT stride = sizeof(ModelVertex), offset = 0;
    m_Ctx->IASetInputLayout(m_IL);
    m_Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Ctx->IASetVertexBuffers(0, 1, &m_VB, &stride, &offset);
    m_Ctx->VSSetShader(m_VS, nullptr, 0);
    m_Ctx->VSSetConstantBuffers(0, 1, &m_CB);
    m_Ctx->PSSetShader(m_PS, nullptr, 0);
    m_Ctx->PSSetConstantBuffers(0, 1, &m_CB);
    if (m_TexSRV) m_Ctx->PSSetShaderResources(0, 1, &m_TexSRV);
    if (m_Sampler) m_Ctx->PSSetSamplers(0, 1, &m_Sampler);

    float bf[4]{};
    upload_cb(1.0f);
    m_Ctx->OMSetDepthStencilState(m_DSState, 0);
    m_Ctx->OMSetBlendState(m_BlendOpaque, bf, 0xFFFFFFFF);

    if (m_SubMeshCount > 0)
    {
        // Multi-material model: draw each slice with its own texture SRV.
        for (int i = 0; i < m_SubMeshCount; ++i)
        {
            if (m_SubMeshes[i].count == 0)
                continue;
            ID3D11ShaderResourceView* srv = m_SubMeshes[i].srv
                ? m_SubMeshes[i].srv : m_TexSRV;
            if (srv) m_Ctx->PSSetShaderResources(0, 1, &srv);
            m_Ctx->Draw(m_SubMeshes[i].count, m_SubMeshes[i].base);
        }
        // Restore a valid bound texture for the wing pass below.
        if (m_TexSRV) m_Ctx->PSSetShaderResources(0, 1, &m_TexSRV);
    }
    else if (m_BodyVertCount > 0)
    {
        m_Ctx->Draw(m_BodyVertCount, 0);
    }

	float wingA = WingAlpha();
	if (m_WingVertCount > 0 && wingA > 0.001f)
	{
		upload_cb(wingA);
		m_Ctx->OMSetDepthStencilState(m_DSNoWrite, 0);
		m_Ctx->OMSetBlendState(m_BlendAlpha, bf, 0xFFFFFFFF);
		m_Ctx->Draw(m_WingVertCount, m_BodyVertCount);
	}

	if (m_SampleCount > 1 && m_ResolveTex)
		m_Ctx->ResolveSubresource(m_ResolveTex, 0, m_RTTex, 0, DXGI_FORMAT_R8G8B8A8_UNORM);

	else if (m_ResolveTex)
		m_Ctx->CopyResource(m_ResolveTex, m_RTTex);

    m_Ctx->OMSetRenderTargets(1, &prevRTV, prevDSV);
    m_Ctx->RSSetState(prevRS);
    m_Ctx->OMSetDepthStencilState(prevDS, prevDSRef);
    m_Ctx->OMSetBlendState(prevBS, prevBF, prevSM);
    m_Ctx->RSSetViewports(1, &prevVP);
    if (prevRTV) prevRTV->Release();
    if (prevDSV) prevDSV->Release();
    if (prevRS) prevRS->Release();
    if (prevDS) prevDS->Release();
    if (prevBS) prevBS->Release();
}

