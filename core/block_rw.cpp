// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//#include <ctime>
#include "block_crypt.h"
#include "../utility/serialize.h"
#include "../pow/impl/utilstrencodings.h"
#include "../core/serialization_adapters.h"
#include "aes.h"

namespace beam
{
	/////////////
	// RW
	const char* const Block::BodyBase::RW::s_pszSufix[Type::count] = {
#define THE_MACRO(x) #x,
		MBLOCK_DATA_Types(THE_MACRO)
#undef THE_MACRO
	};

	void Block::BodyBase::RW::GetPath(std::string& s, int iData) const
	{
		assert(iData < Type::count);
		s = m_sPath + s_pszSufix[iData];
	}

	void Block::BodyBase::RW::ROpen()
	{
		Open(true);
	}

	void Block::BodyBase::RW::WCreate()
	{
		Open(false);
	}

	void Block::BodyBase::RW::Open(bool bRead)
	{
		using namespace std;

		m_bRead = bRead;

		if (bRead)
		{
			static_assert(Type::hd == 0, ""); // must be the 1st to open

			for (int i = 0; i < Type::count; i++)
				OpenInternal(i);
		}
		else
			Delete();
	}

	bool Block::BodyBase::RW::OpenInternal(int iData)
	{
		std::string s;
		GetPath(s, iData);
		if (!m_pS[iData].Open(s.c_str(), m_bRead))
			return false;

		PostOpen(iData);
		return true;
	}

	void Block::BodyBase::RW::PostOpen(int iData)
	{
		if (m_bRead)
		{
			yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(m_pS[iData]);
			ECC::Hash::Value hv;
			arc & hv;

			if (Type::hd == iData)
			{
				if (hv != Rules::get().Checksum)
					throw std::runtime_error("Block rules mismatch");

				arc & m_hvContentTag;
			}
			else
			{
				if (hv != m_hvContentTag)
					throw std::runtime_error("MB tags mismatch");
			}
		}
		else
		{
			yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(m_pS[iData]);

			if (Type::hd == iData)
				arc & Rules::get().Checksum;

			arc & m_hvContentTag;
		}
	}

	void Block::BodyBase::RW::Delete()
	{
		for (int i = 0; i < Type::count; i++)
		{
			std::string s;
			GetPath(s, i);
			DeleteFile(s.c_str());
		}
	}

	void Block::BodyBase::RW::Close()
	{
		for (int i = 0; i < Type::count; i++)
			m_pS[i].Close();
	}

	Block::BodyBase::RW::~RW()
	{
		if (m_bAutoDelete)
		{
			Close();
			Delete();
		}
	}

	void Block::BodyBase::RW::Reset()
	{
		for (int i = 0; i < Type::count; i++)
			if (m_pS[i].IsOpen())
			{
				m_pS[i].Restart();
				PostOpen(i);
			}

		// preload
		LoadInternal(m_pUtxoIn,		Type::ui, m_pGuardUtxoIn);
		LoadInternal(m_pUtxoOut,	Type::uo, m_pGuardUtxoOut);
		LoadInternal(m_pKernelIn,	Type::ki, m_pGuardKernelIn);
		LoadInternal(m_pKernelOut,	Type::ko, m_pGuardKernelOut);
	}

	void Block::BodyBase::RW::Flush()
	{
		for (int i = 0; i < Type::count; i++)
			if (m_pS[i].IsOpen())
				m_pS[i].Flush();
	}

	void Block::BodyBase::RW::Clone(Ptr& pOut)
	{
		RW* pRet = new RW;
		pOut.reset(pRet);

		pRet->m_sPath = m_sPath;
		pRet->Open(m_bRead);
	}

	void Block::BodyBase::RW::NextUtxoIn()
	{
		LoadInternal(m_pUtxoIn, Type::ui, m_pGuardUtxoIn);
	}

	void Block::BodyBase::RW::NextUtxoOut()
	{
		LoadInternal(m_pUtxoOut, Type::uo, m_pGuardUtxoOut);
	}

	void Block::BodyBase::RW::NextKernelIn()
	{
		LoadInternal(m_pKernelIn, Type::ki, m_pGuardKernelIn);
	}

	void Block::BodyBase::RW::NextKernelOut()
	{
		LoadInternal(m_pKernelOut, Type::ko, m_pGuardKernelOut);
	}

	void Block::BodyBase::RW::get_Start(BodyBase& body, SystemState::Sequence::Prefix& prefix)
	{
		if (!m_pS[Type::hd].IsOpen())
			std::ThrowIoError();
		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(m_pS[Type::hd]);

		arc & body;
		arc & prefix;
	}

	bool Block::BodyBase::RW::get_NextHdr(SystemState::Sequence::Element& elem)
	{
		std::FStream& s = m_pS[Type::hd];
		if (!s.get_Remaining())
			return false;

		yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
		arc & elem;

		return true;
	}

	void Block::BodyBase::RW::WriteIn(const Input& v)
	{
		WriteInternal(v, Type::ui);
	}

	void Block::BodyBase::RW::WriteIn(const TxKernel& v)
	{
		WriteInternal(v, Type::ki);
	}

	void Block::BodyBase::RW::WriteOut(const Output& v)
	{
		WriteInternal(v, Type::uo);
	}

	void Block::BodyBase::RW::WriteOut(const TxKernel& v)
	{
		WriteInternal(v, Type::ko);
	}

	void Block::BodyBase::RW::put_Start(const BodyBase& body, const SystemState::Sequence::Prefix& prefix)
	{
		WriteInternal(body, Type::hd);
		WriteInternal(prefix, Type::hd);
	}

	void Block::BodyBase::RW::put_NextHdr(const SystemState::Sequence::Element& elem)
	{
		WriteInternal(elem, Type::hd);
	}

	template <typename T>
	void Block::BodyBase::RW::LoadInternal(const T*& pPtr, int iData, typename T::Ptr* ppGuard)
	{
		std::FStream& s = m_pS[iData];

		if (s.IsOpen() && s.get_Remaining())
		{
			ppGuard[0].swap(ppGuard[1]);
			//if (!ppGuard[0])
				ppGuard[0].reset(new T);

			CommitmentAndMaturity::SerializeMaturity scope(true);
			yas::binary_iarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
			arc & *ppGuard[0];

			pPtr = ppGuard[0].get();
		}
		else
			pPtr = NULL;
	}

	template <typename T>
	void Block::BodyBase::RW::WriteInternal(const T& v, int iData)
	{
		std::FStream& s = m_pS[iData];
		if (!s.IsOpen() && !OpenInternal(iData))
			std::ThrowIoError();

		CommitmentAndMaturity::SerializeMaturity scope(true);
		yas::binary_oarchive<std::FStream, SERIALIZE_OPTIONS> arc(s);
		arc & v;
	}

	void TxBase::IWriter::Dump(IReader&& r)
	{
		r.Reset();

		for (; r.m_pUtxoIn; r.NextUtxoIn())
			WriteIn(*r.m_pUtxoIn);
		for (; r.m_pUtxoOut; r.NextUtxoOut())
			WriteOut(*r.m_pUtxoOut);
		for (; r.m_pKernelIn; r.NextKernelIn())
			WriteIn(*r.m_pKernelIn);
		for (; r.m_pKernelOut; r.NextKernelOut())
			WriteOut(*r.m_pKernelOut);
	}

	bool TxBase::IWriter::Combine(IReader&& r0, IReader&& r1, const volatile bool& bStop)
	{
		IReader* ppR[] = { &r0, &r1 };
		return Combine(ppR, _countof(ppR), bStop);
	}

	bool TxBase::IWriter::Combine(IReader** ppR, int nR, const volatile bool& bStop)
	{
		for (int i = 0; i < nR; i++)
			ppR[i]->Reset();

		// Utxo
		while (true)
		{
			if (bStop)
				return false;

			const Input* pInp = NULL;
			const Output* pOut = NULL;
			int iInp = 0, iOut = 0; // initialized just to suppress the warning, not really needed

			for (int i = 0; i < nR; i++)
			{
				const Input* pi = ppR[i]->m_pUtxoIn;
				if (pi && (!pInp || (*pInp > *pi)))
				{
					pInp = pi;
					iInp = i;
				}

				const Output* po = ppR[i]->m_pUtxoOut;
				if (po && (!pOut || (*pOut > *po)))
				{
					pOut = po;
					iOut = i;
				}
			}

			if (pInp)
			{
				if (pOut)
				{
					int n = CmpInOut(*pInp, *pOut);
					if (n > 0)
						pInp = NULL;
					else
						if (!n)
						{
							// skip both
							ppR[iInp]->NextUtxoIn();
							ppR[iOut]->NextUtxoOut();
							continue;
						}
				}
			}
			else
				if (!pOut)
					break;


			if (pInp)
			{
				WriteIn(*pInp);
				ppR[iInp]->NextUtxoIn();
			}
			else
			{
				WriteOut(*pOut);
				ppR[iOut]->NextUtxoOut();
			}
		}


		// Kernels
		while (true)
		{
			if (bStop)
				return false;

			const TxKernel* pInp = NULL;
			const TxKernel* pOut = NULL;
			int iInp = 0, iOut = 0; // initialized just to suppress the warning, not really needed

			for (int i = 0; i < nR; i++)
			{
				const TxKernel* pi = ppR[i]->m_pKernelIn;
				if (pi && (!pInp || (*pInp > *pi)))
				{
					pInp = pi;
					iInp = i;
				}

				const TxKernel* po = ppR[i]->m_pKernelOut;
				if (po && (!pOut || (*pOut > *po)))
				{
					pOut = po;
					iOut = i;
				}
			}

			if (pInp)
			{
				if (pOut)
				{
					int n = pInp->cmp(*pOut);
					if (n > 0)
						pInp = NULL;
					else
						if (!n)
						{
							// skip both
							ppR[iInp]->NextUtxoIn();
							ppR[iOut]->NextUtxoOut();
							continue;
						}
				}
			}
			else
				if (!pOut)
					break;


			if (pInp)
			{
				WriteIn(*pInp);
				ppR[iInp]->NextKernelIn();
			}
			else
			{
				WriteOut(*pOut);
				ppR[iOut]->NextKernelOut();
			}
		}

		return true;
	}

	bool Block::BodyBase::IMacroWriter::CombineHdr(IMacroReader&& r0, IMacroReader&& r1, const volatile bool& bStop)
	{
		Block::BodyBase body0, body1;
		Block::SystemState::Sequence::Prefix prefix0, prefix1;
		Block::SystemState::Sequence::Element elem;

		r0.Reset();
		r0.get_Start(body0, prefix0);
		r1.Reset();
		r1.get_Start(body1, prefix1);

		body0.Merge(body1);
		put_Start(body0, prefix0);

		while (r0.get_NextHdr(elem))
		{
			if (bStop)
				return false;
			put_NextHdr(elem);
		}

		while (r1.get_NextHdr(elem))
		{
			if (bStop)
				return false;
			put_NextHdr(elem);
		}

		return true;
	}

	/////////////
	// KeyString
	void KeyString::Export(const ECC::HKdf& v)
	{
		ECC::NoLeak<ECC::HKdf::Packed> p;
		v.Export(p.V);
		Export(&p.V, sizeof(p.V), 's');
	}

	void KeyString::Export(const ECC::HKdfPub& v)
	{
		ECC::NoLeak<ECC::HKdfPub::Packed> p;
		v.Export(p.V);
		Export(&p.V, sizeof(p.V), 'P');
	}

	void KeyString::Export(void* p, uint32_t nData, uint8_t nCode)
	{
		ByteBuffer bb;
		bb.resize(sizeof(MacValue) + nData + 1 + m_sMeta.size());
		MacValue& mv = reinterpret_cast<MacValue&>(bb.at(0));

		bb[sizeof(MacValue)] = nCode;
		memcpy(&bb.at(1) + sizeof(MacValue), p, nData);
		memcpy(&bb.at(1) + sizeof(MacValue) + nData, m_sMeta.c_str(), m_sMeta.size());

		XCrypt(mv, static_cast<uint32_t>(nData + 1 + m_sMeta.size()), true);

		m_sRes = EncodeBase64(&bb.at(0), bb.size());
	}

	bool KeyString::Import(ECC::HKdf& v)
	{
		ECC::NoLeak<ECC::HKdf::Packed> p;
		return
			Import(&p.V, sizeof(p.V), 's') &&
			v.Import(p.V);
	}

	bool KeyString::Import(ECC::HKdfPub& v)
	{
		ECC::NoLeak<ECC::HKdfPub::Packed> p;
		return
			Import(&p.V, sizeof(p.V), 'P') &&
			v.Import(p.V);
	}

	bool KeyString::Import(void* p, uint32_t nData, uint8_t nCode)
	{
		bool bInvalid = false;
		ByteBuffer bb = DecodeBase64(m_sRes.c_str(), &bInvalid);

		if (bInvalid || (bb.size() < sizeof(MacValue) + 1 + nData))
			return false;

		MacValue& mv = reinterpret_cast<MacValue&>(bb.at(0));
		MacValue mvOrg = mv;

		XCrypt(mv, static_cast<uint32_t>(bb.size()) - sizeof(mv), false);

		if ((mv != mvOrg) || (bb[sizeof(MacValue)] != nCode))
			return false;

		memcpy(p, &bb.at(1) + sizeof(MacValue), nData);

		m_sMeta.resize(bb.size() - (sizeof(MacValue) + 1 + nData));
		if (!m_sMeta.empty())
			memcpy(&m_sMeta.front(), &bb.at(1) + sizeof(MacValue) + nData, m_sMeta.size());

		return true;
	}

	void KeyString::XCrypt(MacValue& mv, uint32_t nSize, bool bEnc) const
	{
		static_assert(AES::s_KeyBytes == sizeof(m_hvSecret.V), "");
		AES::Encoder enc;
		enc.Init(m_hvSecret.V.m_pData);

		AES::StreamCipher c;
		ECC::NoLeak<ECC::Hash::Value> hvIV;
		ECC::Hash::Processor() << m_hvSecret.V >> hvIV.V;

		c.m_Counter = hvIV.V; // truncated
		c.m_nBuf = 0;

		ECC::Hash::Mac hmac;
		hmac.Reset(m_hvSecret.V.m_pData, m_hvSecret.V.nBytes);

		if (bEnc)
			hmac.Write(reinterpret_cast<uint8_t*>(&mv + 1), nSize);

		c.XCrypt(enc, reinterpret_cast<uint8_t*>(&mv + 1), nSize);

		if (!bEnc)
			hmac.Write(reinterpret_cast<uint8_t*>(&mv + 1), nSize);

		hmac >> hvIV.V;
		mv = hvIV.V;
	}


} // namespace beam
