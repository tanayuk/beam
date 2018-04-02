#pragma once

#include <vector>
#include <array>
#include <list>
#include <utility>
#include <cstdint>
#include <memory>

#include "ecc.h"

namespace beam
{
	// sorry for replacing 'using' by 'typedefs', some compilers don't support it
	typedef uint64_t Timestamp;
	typedef uint64_t Difficulty;
	typedef uint64_t Height;

	namespace Merkle {
		struct Hash;

		typedef std::pair<bool, Hash>	Node;
		typedef std::list<Node>			Proof;

		struct Hash :public ECC::Hash::Value
		{
			void Interpret(const Proof&);
		};
	}


	struct Input
	{
		typedef std::unique_ptr<Input> Ptr;

		ECC::Point	m_Commitment;
		bool		m_Coinbase;
		Height		m_Height;

		// In case there are multiple UTXOs with the same commitment value (which we permit) the height should be used to distinguish between them
		// If not specified (no UTXO with the specified height) - it will automatically be selected.

		void get_Hash(Merkle::Hash&) const;
		bool IsValidProof(const Merkle::Proof&, const Merkle::Hash& root) const;
	};

	struct Output
	{
		typedef std::unique_ptr<Output> Ptr;

		ECC::Point	m_Commitment;
		bool		m_Coinbase;

		// one of the following *must* be specified
		struct Condidential {
			ECC::RangeProof m_RangeProof;
		};

		struct Public {
			uint64_t m_Value;
			ECC::Signature m_Signature;
		};

		std::unique_ptr<Condidential>	m_pCondidential;
		std::unique_ptr<Public>			m_pPublic;
	};


	struct TxKernel
	{
		typedef std::unique_ptr<TxKernel> Ptr;

		// Mandatory
		ECC::Point		m_Excess;
		ECC::Signature	m_Signature;

		// Optional
		std::unique_ptr<uint64_t>			m_pFee;
		std::unique_ptr<Height>				m_pHeight;
		std::unique_ptr<ECC::Hash::Value>	m_pCustomMsg;
		std::unique_ptr<ECC::Point>			m_pPublicKey;

		std::list<Ptr> m_vNested; // nested kernels, included in the signature.

		void CalculateSignature();
		bool IsValid() const;

		void get_Hash(Merkle::Hash&) const;
		bool IsValidProof(const Merkle::Proof&, const Merkle::Hash& root) const;
	};

	struct TxBase
	{
		std::list<Input::Ptr> m_vInputs;
		std::list<Output::Ptr> m_vOutputs;
		std::list<TxKernel::Ptr> m_vKernels;
		ECC::Scalar m_Offset;
	};

	struct Transaction
		:public TxBase
	{
		// tests the validity of all the components, and overall arithmetics.
		// Does *not* check the existence of the input UTXOs
		// Explicit fees are considered "lost" in the transactions (i.e. would be collected by the miner)
		bool IsValid() const;
	};

	struct Block
	{
		// Different parts of the block are split into different structs, so that they can be manipulated (transferred, processed, saved and etc.) independently
		// For instance, there is no need to keep PoW (at least in SPV client) once it has been validated.

		// TBD: decide the serialization format. Basically it consists entirely of structs and ordinal data types, can be stored as-is. Only the matter of big/little-endian should be defined.

		struct Header
		{
			ECC::Hash::Value	m_HashPrev;
			Merkle::Hash		m_FullDescription; // merkle hash
		    Height				m_Height; // of this specific block
		    Timestamp			m_TimeStamp;
		    Difficulty			m_TotalDifficulty;
			uint8_t				m_Difficulty; // of this specific block
		};

		struct PoW
		{
			// equihash parameters
			static const uint32_t N = 200;
			static const uint32_t K = 9;

			static const uint32_t nNumIndices		= 1 << K; // 512 
			static const uint32_t nBitsPerIndex		= N / (K + 1); // 20. actually tha last index may be wider (equal to max bound), but since indexes are sorted it can be encoded as 0.

			static const uint32_t nSolutionBits		= nNumIndices * nBitsPerIndex;

			static_assert(!(nSolutionBits & 7), "PoW solution should be byte-aligned");
			static const uint32_t nSolutionBytes	= nSolutionBits >> 3; // 1280 bytes

			ECC::uintBig_t<256>	m_Nonce;
			uint8_t				m_Difficulty;
			uint8_t				m_pSolution[nSolutionBytes];

			bool IsValid(const Header&) const;
		};

		struct Body
			:public TxBase
		{
			// TODO: additional parameters, such as block explicit subsidy, sidechains and etc.

			// Test the following:
			//		Validity of all the components, and overall arithmetics, whereas explicit fees are already collected by extra UTXO(s) put by the miner
			//		All components are specified in a lexicographical order, to conceal the actual transaction graph
			// Not tested by this function (but should be tested by nodes!)
			//		Existence of all the input UTXOs, and their "liquidity" (by the policy UTXO liquidity may be restricted wrt its maturity)
			//		Existence of the coinbase non-confidential output UTXO, with the sum amount equal to the new coin emission.
			//		Existence of the treasury output UTXO, if needed by the policy.
			bool IsValid() const;
		};
	};
}
