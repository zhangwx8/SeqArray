// ===========================================================
//
// ReadByVariant.cpp: Read data variant by variant
//
// Copyright (C) 2013-2015    Xiuwen Zheng
//
// This file is part of SeqArray.
//
// SeqArray is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 3 as
// published by the Free Software Foundation.
//
// SeqArray is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SeqArray.
// If not, see <http://www.gnu.org/licenses/>.

#include "ReadByVariant.h"


/// 
static void MAP_INDEX(PdSequenceX Node, const vector<C_BOOL> &sel,
	vector<int> &out_len, vector<C_BOOL> &out_var_sel,
	C_Int32 &out_var_start, C_Int32 &out_var_count)
{
	if (GDS_Array_DimCnt(Node) != 1)
		throw ErrSeqArray("Invalid dimension.");
	C_Int64 Cnt = GDS_Array_GetTotalCount(Node);

	if (sel.empty())
	{
		out_len.resize(Cnt);
		C_Int32 _st=0, _cnt=Cnt;
		GDS_Array_ReadData(Node, &_st, &_cnt, &out_len[0], svInt32);

		out_var_start = 0;
		out_var_count = 0;
		for (vector<int>::iterator it=out_len.begin();
			it != out_len.end(); it++)
		{
			if (*it > 0) out_var_count += *it;
		}
		out_var_sel.clear();
		out_var_sel.resize(out_var_count, TRUE);

	} else {
		// check
		if ((int)sel.size() != Cnt)
			throw ErrSeqArray("Invalid dimension.");

		// find the start
		int _start = 0;
		for (; _start < (int)sel.size(); _start++)
			if (sel[_start]) break;
		// find the end
		int _end = sel.size()-1;
		for (; _end >= 0; _end --)
			if (sel[_end]) break;

		if (_end >= 0)
		{
			const int N_MAX = 16384;
			C_Int32 buffer[N_MAX];

			out_var_start = 0;
			int pos = 0;
			while (pos < _start)
			{
				int L = _start - pos;
				if (L > N_MAX) L = N_MAX;
				GDS_Array_ReadData(Node, &pos, &L, buffer, svInt32);
				pos += L;
				for (int i=0; i < L; i++)
				{
					if (buffer[i] > 0)
						out_var_start += buffer[i];
				}
			}

			out_len.clear();
			out_var_sel.clear();
			while (pos <= _end)
			{
				int L = _end - pos + 1;
				if (L > N_MAX) L = N_MAX;
				GDS_Array_ReadData(Node, &pos, &L, buffer, svInt32);
				for (int i=0; i < L; i++)
				{
					int LL = (buffer[i] > 0) ? buffer[i] : 0;
					if (sel[pos+i])
					{
						out_len.push_back(LL);
						for (int j=0; j < LL; j++)
							out_var_sel.push_back(TRUE);
					} else {
						for (int j=0; j < LL; j++)
							out_var_sel.push_back(FALSE);
					}
				}
				pos += L;
			}
			
			out_var_count = out_var_sel.size();
		} else {
			out_len.clear(); out_var_sel.clear();
			out_var_start = out_var_count = 0;
		}
	}
}



// ===================================================================== //

/// 
CVarApplyByVariant::CVarApplyByVariant()
{
	Node = IndexNode = NULL;
	VariantSelect = NULL;
}

void CVarApplyByVariant::InitObject(TType Type, const char *Path,
	PdGDSObj Root, int nVariant, C_BOOL *VariantSel, int nSample,
	C_BOOL *SampleSel)
{
	static const char *ErrDim = "Invalid dimension of '%s'.";

	// initialize
	GDS_PATH_PREFIX_CHECK(Path);
	VarType = Type;
	Node = GDS_Node_Path(Root, Path, TRUE);
	SVType = GDS_Array_GetSVType(Node);
	DimCnt = GDS_Array_DimCnt(Node);

	TotalNum_Variant = nVariant;
	VariantSelect = VariantSel;
	Num_Sample = NUM_OF_TRUE(SampleSel, nSample);

	string Path2; // the path with '@'

	switch (Type)
	{
		case ctBasic:
			// VARIABLE: variant.id, position, allele
			if ((DimCnt != 1) || (GDS_Array_GetTotalCount(Node) != nVariant))
				throw ErrSeqArray(ErrDim, Path);
			break;

		case ctGenotype:
			// VARIABLE: genotype/data, genotype/@data
			if (DimCnt != 3)
				throw ErrSeqArray(ErrDim, Path);
			GDS_Array_GetDim(Node, DLen, 3);
			if ((DLen[0] < nVariant) || (DLen[1] != nSample))
				throw ErrSeqArray(ErrDim, Path);

			Path2 = GDS_PATH_PREFIX(Path, '@');
			IndexNode = GDS_Node_Path(Root, Path2.c_str(), FALSE);
			if (IndexNode == NULL)
				throw ErrSeqArray("'%s' is missing!", Path2.c_str());
			if ((GDS_Array_DimCnt(IndexNode) != 1) ||
					(GDS_Array_GetTotalCount(IndexNode) != nVariant))
				throw ErrSeqArray(ErrDim, Path2.c_str());

			CellCount = Num_Sample * DLen[2];
			Init.Need_GenoBuffer(DLen[1] * DLen[2]);

			SelPtr[1] = SampleSel;
			SelPtr[2] = NeedTRUE(DLen[2]);
			break;

		case ctPhase:
			// VARIABLE: phase/data
			if ((DimCnt != 2) && (DimCnt != 3))
				throw ErrSeqArray(ErrDim, Path);
			GDS_Array_GetDim(Node, DLen, 3);
			if ((DLen[0] != nVariant) || (DLen[1] != nSample))
				throw ErrSeqArray(ErrDim, Path);

			SelPtr[1] = SampleSel;
			if (DimCnt > 2)
				SelPtr[2] = NeedTRUE(DLen[2]);
			break;

		case ctInfo:
			// VARIABLE: info/...
			if ((DimCnt!=1) && (DimCnt!=2))
				throw ErrSeqArray(ErrDim, Path);
			GDS_Array_GetDim(Node, DLen, 2);

			Path2 = GDS_PATH_PREFIX(Path, '@');
			IndexNode = GDS_Node_Path(Root, Path2.c_str(), FALSE);
			if (IndexNode != NULL)
			{
				if ((GDS_Array_DimCnt(IndexNode) != 1) || (GDS_Array_GetTotalCount(IndexNode) != nVariant))
					throw ErrSeqArray(ErrDim, Path2.c_str());
			} else {
				if (DLen[0] != nVariant)
					throw ErrSeqArray(ErrDim, Path);
			}

			if (DimCnt > 1)
				SelPtr[1] = NeedTRUE(DLen[1]);
			break;

		case ctFormat:
			// VARIABLE: format/...
			if ((DimCnt!=2) && (DimCnt!=3))
				throw ErrSeqArray(ErrDim, Path);
			GDS_Array_GetDim(Node, DLen, 3);

			Path2 = GDS_PATH_PREFIX(Path, '@');
			IndexNode = GDS_Node_Path(Root, Path2.c_str(), FALSE);
			if (IndexNode != NULL)
			{
				if ((GDS_Array_DimCnt(IndexNode) != 1) || (GDS_Array_GetTotalCount(IndexNode) != nVariant))
					throw ErrSeqArray(ErrDim, Path2.c_str());
			} else
				throw ErrSeqArray("'%s' is missing!", Path2.c_str());

			SelPtr[1] = SampleSel;
			if (DimCnt > 2)
				SelPtr[2] = NeedTRUE(DLen[2]);
			break;

		default:
			throw ErrSeqArray("Internal Error in 'CVarApplyByVariant::InitObject'.");
	}

	ResetObject();
}

void CVarApplyByVariant::ResetObject()
{
	CurIndex = 0;
	IndexRaw = 0;
	if (IndexNode)
	{
		C_Int32 Cnt=1;
		GDS_Array_ReadData(IndexNode, &CurIndex, &Cnt, &NumIndexRaw, svInt32);
		if (NumIndexRaw < 0) NumIndexRaw = 0;
	} else
		NumIndexRaw = 1;

	if (!VariantSelect[0])
		NextCell();
}

bool CVarApplyByVariant::NextCell()
{
	CurIndex ++;

	if (IndexNode)
	{
		IndexRaw += NumIndexRaw;
		C_Int32 Cnt=1, L;
		while ((CurIndex<TotalNum_Variant) && !VariantSelect[CurIndex])
		{
			GDS_Array_ReadData(IndexNode, &CurIndex, &Cnt, &L, svInt32);
			if (L > 0)
				IndexRaw += L;
			CurIndex ++;
		}
		if (CurIndex < TotalNum_Variant)
		{
			GDS_Array_ReadData(IndexNode, &CurIndex, &Cnt, &NumIndexRaw,
				svInt32);
			if (NumIndexRaw < 0) NumIndexRaw = 0;
		} else
			NumIndexRaw = 0;
	} else {
		while ((CurIndex<TotalNum_Variant) && !VariantSelect[CurIndex])
			CurIndex ++;
		IndexRaw = CurIndex;
		NumIndexRaw = 1;
	}

	return (CurIndex < TotalNum_Variant);
}

void CVarApplyByVariant::ReadGenoData(int *Base)
{
	// the size of Init.GENO_BUFFER has been check in 'Init()'
	ssize_t SlideCnt = DLen[1]*DLen[2];

	TdIterator it;
	GDS_Iter_GetStart(Node, &it);
	GDS_Iter_Offset(&it, C_Int64(IndexRaw)*SlideCnt);
	GDS_Iter_RData(&it, &Init.GENO_BUFFER[0], SlideCnt, svUInt8);
	C_UInt8 *s = &Init.GENO_BUFFER[0];
	int *p = Base;
	for (int i=0; i < DLen[1]; i++)
	{
		if (SelPtr[1][i])
		{
			for (int j=0; j < DLen[2]; j++)
				*p++ = *s++;
		} else {
			s += DLen[2];
		}
	}

	int missing = 3;

	// CellCount = Num_Sample * DLen[2] in 'NeedRData'
	for (int idx=1; idx < NumIndexRaw; idx ++)
	{
		GDS_Iter_GetStart(Node, &it);
		GDS_Iter_Offset(&it, (C_Int64(IndexRaw) + idx)*SlideCnt);
		GDS_Iter_RData(&it, &Init.GENO_BUFFER[0], SlideCnt, svUInt8);

		int shift = idx*2;
		s = &Init.GENO_BUFFER[0];
		p = Base;
		for (int i=0; i < DLen[1]; i++)
		{
			if (SelPtr[1][i])
			{
				for (int j=0; j < DLen[2]; j++)
				{
					*p |= int(*s) << shift;
					p ++; s ++;
				}
			} else {
				s += DLen[2];
			}
		}

		missing = (missing << 2) | 0x03;
	}
	for (size_t n=CellCount; n > 0; n--)
	{
		if (*Base == missing) *Base = NA_INTEGER;
		Base ++;
	}	
}

void CVarApplyByVariant::ReadData(SEXP Val)
{
	if (NumIndexRaw <= 0) return;
	if (VarType == ctGenotype)
	{
		ReadGenoData(INTEGER(Val));
	} else {
		C_Int32 st[3] = { IndexRaw, 0, 0 };
		DLen[0] = NumIndexRaw;
		SelPtr[0] = NeedTRUE(NumIndexRaw);

		if (COREARRAY_SV_INTEGER(SVType))
		{
			GDS_Array_ReadDataEx(Node, st, DLen, SelPtr, INTEGER(Val), svInt32);
		} else if (COREARRAY_SV_FLOAT(SVType))
		{
			GDS_Array_ReadDataEx(Node, st, DLen, SelPtr, REAL(Val), svFloat64);
		} else if (COREARRAY_SV_STRING(SVType))
		{
			vector<string> buffer(CellCount);
			GDS_Array_ReadDataEx(Node, st, DLen, SelPtr, &buffer[0], svStrUTF8);
			for (int i=0; i < (int)buffer.size(); i++)
				SET_STRING_ELT(Val, i, mkChar(buffer[i].c_str()));
		}
	}
}

SEXP CVarApplyByVariant::NeedRData(int &nProtected)
{
	if (NumIndexRaw <= 0) return R_NilValue;

	map<size_t, SEXP>::iterator it = VarList.find(NumIndexRaw);
	if (it == VarList.end())
	{
		switch (VarType)
		{
		case ctBasic:
			CellCount = 1; break;
		case ctGenotype:
			CellCount = Num_Sample * DLen[2]; break;
		case ctPhase:
			CellCount = (DimCnt>2) ? Num_Sample*DLen[2] : Num_Sample;
			break;
		case ctInfo:
			CellCount = ((DimCnt>1) ? DLen[1] : 1) * NumIndexRaw;
			break;
		case ctFormat:
			CellCount = ((DimCnt>2) ? Num_Sample*DLen[2] : Num_Sample) *
						NumIndexRaw;
			break;
		default:
			CellCount = 0;
		}

		SEXP ans = R_NilValue, dim;
		if (COREARRAY_SV_INTEGER(SVType))
		{
			char classname[32];
			classname[0] = 0;
			GDS_Node_GetClassName(Node, classname, sizeof(classname));
			if (strcmp(classname, "dBit1") == 0)
			{
				PROTECT(ans = NEW_LOGICAL(CellCount));
			} else if (GDS_R_Is_Logical(Node))
			{
				PROTECT(ans = NEW_LOGICAL(CellCount));
			} else {
				PROTECT(ans = NEW_INTEGER(CellCount));
				nProtected += GDS_R_Set_IfFactor(Node, ans);
			}
			nProtected ++;
		} else if (COREARRAY_SV_FLOAT(SVType))
		{
			PROTECT(ans = NEW_NUMERIC(CellCount));
			nProtected ++;
		} else if (COREARRAY_SV_STRING(SVType))
		{
			PROTECT(ans = NEW_CHARACTER(CellCount));
			nProtected ++;
		}

		SEXP name_list, tmp;
		switch (VarType)
		{
		case ctGenotype:
			PROTECT(dim = NEW_INTEGER(2));
			INTEGER(dim)[0] = DLen[2]; INTEGER(dim)[1] = Num_Sample;
			SET_DIM(ans, dim);
			PROTECT(name_list = NEW_LIST(2));
			PROTECT(tmp = NEW_CHARACTER(2));
				SET_STRING_ELT(tmp, 0, mkChar("allele"));
				SET_STRING_ELT(tmp, 1, mkChar("sample"));
				SET_NAMES(name_list, tmp);
			SET_DIMNAMES(ans, name_list);
			nProtected += 3;
			break;

		case ctPhase:
			if (DimCnt > 2)
			{
				PROTECT(dim = NEW_INTEGER(2)); nProtected ++;
				INTEGER(dim)[0] = DLen[2]; INTEGER(dim)[1] = Num_Sample;
				SET_DIM(ans, dim);
			}
			break;

		case ctFormat:
			if (DimCnt == 2)
			{
				PROTECT(dim = NEW_INTEGER(2)); nProtected ++;
				INTEGER(dim)[0] = Num_Sample; INTEGER(dim)[1] = NumIndexRaw;
				SET_DIM(ans, dim);
			} else if (DimCnt > 2)
			{
				PROTECT(dim = NEW_INTEGER(3)); nProtected ++;
				INTEGER(dim)[0] = DLen[2]; INTEGER(dim)[1] = Num_Sample;
				INTEGER(dim)[2] = NumIndexRaw;
				SET_DIM(ans, dim);
			}
			break;

		default:
			break;
		}

		VarList.insert(pair<int, SEXP>(NumIndexRaw, ans));
		return ans;
	} else
		return it->second;
}



extern "C"
{
// ===========================================================
// Get data from a working space
// ===========================================================

static SEXP VAR_LOGICAL(PdGDSObj Node, SEXP Array)
{
	char classname[32];
	classname[0] = 0;
	GDS_Node_GetClassName(Node, classname, sizeof(classname));
	if (strcmp(classname, "dBit1") == 0)
	{
		PROTECT(Array);
		Array = AS_LOGICAL(Array);
		UNPROTECT(1);
	}
	return Array;
}

/// Get data from a working space
COREARRAY_DLL_EXPORT SEXP sqa_GetData(SEXP gdsfile, SEXP var_name)
{
	COREARRAY_TRY

		SEXP tmp;

		// the selection
		TInitObject::TSelection &Sel = Init.Selection(gdsfile);
		// the GDS root node
		PdGDSObj Root = GDS_R_SEXP2Obj(GetListElement(gdsfile, "root"));

		// 
		C_BOOL *SelPtr[256];
		int DimCnt, DStart[256], DLen[256];

		// the path of GDS variable
		const char *s = CHAR(STRING_ELT(var_name, 0));
		if (strcmp(s, "sample.id") == 0)
		{
			// ===========================================================
			// sample.id

			PdSequenceX N = GDS_Node_Path(Root, s, TRUE);
			DimCnt = GDS_Array_DimCnt(N);
			if (DimCnt != 1)
				throw ErrSeqArray("Invalid dimension of 'sample.id'.");
			if (Sel.Sample.empty())
			{
				rv_ans = GDS_R_Array_Read(N, NULL, NULL, NULL, 0);
			} else {
				GDS_Array_GetDim(N, DLen, 1);
				if ((int)Sel.Sample.size() != DLen[0])
					throw ErrSeqArray("Invalid dimension of 'sample.id'.");
				SelPtr[0] = &Sel.Sample[0];
				rv_ans = GDS_R_Array_Read(N, NULL, NULL, &SelPtr[0], 0);
			}

		} else if ( (strcmp(s, "variant.id")==0) || (strcmp(s, "position")==0) ||
			(strcmp(s, "chromosome")==0) || (strcmp(s, "allele")==0) ||
			(strcmp(s, "annotation/id")==0) || (strcmp(s, "annotation/qual")==0) ||
			(strcmp(s, "annotation/filter")==0) )
		{
			// ===========================================================
			// variant.id, position, chromosome, allele, annotation/id
			// annotation/qual, annotation/filter

			PdSequenceX N = GDS_Node_Path(Root, s, TRUE);
			DimCnt = GDS_Array_DimCnt(N);
			if (DimCnt != 1)
				throw ErrSeqArray("Invalid dimension of '%s'.", s);
			if (Sel.Variant.empty())
			{
				rv_ans = GDS_R_Array_Read(N, NULL, NULL, NULL, 0);
			} else {
				GDS_Array_GetDim(N, DLen, 1);
				if ((int)Sel.Variant.size() != DLen[0])
					throw ErrSeqArray("Invalid dimension of '%s'.", s);
				SelPtr[0] = &Sel.Variant[0];
				rv_ans = GDS_R_Array_Read(N, NULL, NULL, &SelPtr[0], 0);
			}

		} else if (strcmp(s, "phase") == 0)
		{
			// ===========================================================
			// phase/

			PdSequenceX N = GDS_Node_Path(Root, "phase/data", TRUE);
			DimCnt = GDS_Array_DimCnt(N);
			if ((DimCnt != 2) && (DimCnt != 3))
				throw ErrSeqArray("Invalid dimension of '%s'.", s);
			if (!Sel.Sample.empty() || !Sel.Variant.empty())
			{
				GDS_Array_GetDim(N, DLen, 3);

				if (Sel.Variant.empty())
					Sel.Variant.resize(DLen[0], TRUE);
				else if ((int)Sel.Variant.size() != DLen[0])
					throw ErrSeqArray("Invalid dimension of '%s'.", s);

				if (Sel.Sample.empty())
					Sel.Sample.resize(DLen[1], TRUE);
				else if ((int)Sel.Sample.size() != DLen[1])
					throw ErrSeqArray("Invalid dimension of '%s'.", s);

				CVarApply Var;

				SelPtr[0] = &Sel.Variant[0];
				SelPtr[1] = &Sel.Sample[0];
				if (DimCnt == 3)
					SelPtr[2] = Var.NeedTRUE(DLen[2]);

				rv_ans = GDS_R_Array_Read(N, NULL, NULL, &SelPtr[0], 0);
			} else {
				rv_ans = GDS_R_Array_Read(N, NULL, NULL, NULL, 0);
			}

		} else if (strcmp(s, "genotype") == 0)
		{
			// ===========================================================
			// genotypic data

			// init selection
			if (Sel.Sample.empty())
			{
				PdSequenceX N = GDS_Node_Path(Root, "sample.id", TRUE);
				int Cnt = GDS_Array_GetTotalCount(N);
				if (Cnt < 0) throw ErrSeqArray("Invalid dimension of 'sample.id'.");
				Sel.Sample.resize(Cnt, TRUE);
			}
			if (Sel.Variant.empty())
			{
				PdSequenceX N = GDS_Node_Path(Root, "variant.id", TRUE);
				int Cnt = GDS_Array_GetTotalCount(N);
				if (Cnt < 0) throw ErrSeqArray("Invalid dimension of 'variant.id'.");
				Sel.Variant.resize(Cnt, TRUE);
			}

			// the number of selected variants
			int nVariant = 0;
			for (vector<C_BOOL>::iterator it = Sel.Variant.begin();
				it != Sel.Variant.end(); it ++)
			{
				if (*it) nVariant ++;
			}
			if (nVariant > 0)
			{
				// initialize the GDS Node list
				CVarApplyByVariant NodeVar;
				NodeVar.InitObject(CVariable::ctGenotype,
					"genotype/data", Root, Sel.Variant.size(),
					&Sel.Variant[0], Sel.Sample.size(), &Sel.Sample[0]);

				// the number of calling PROTECT
				int SIZE = NodeVar.Num_Sample * NodeVar.DLen[2];
				PROTECT(rv_ans = NEW_INTEGER(nVariant * SIZE));
				PROTECT(tmp = NEW_INTEGER(3));
					INTEGER(tmp)[0] = NodeVar.DLen[2];
					INTEGER(tmp)[1] = NodeVar.Num_Sample;
					INTEGER(tmp)[2] = nVariant;
				SET_DIM(rv_ans, tmp);
				SEXP name_list;
				PROTECT(name_list = NEW_LIST(3));
				PROTECT(tmp = NEW_CHARACTER(3));
					SET_STRING_ELT(tmp, 0, mkChar("allele"));
					SET_STRING_ELT(tmp, 1, mkChar("sample"));
					SET_STRING_ELT(tmp, 2, mkChar("variant"));
					SET_NAMES(name_list, tmp);
				SET_DIMNAMES(rv_ans, name_list);

				int *base = INTEGER(rv_ans);
				do {
					NodeVar.ReadGenoData(base);
					base += SIZE;
				} while (NodeVar.NextCell());

				// finally
				UNPROTECT(4);
			}

		} else if (strncmp(s, "annotation/info/", 16) == 0)
		{
			GDS_PATH_PREFIX_CHECK(s);
			PdSequenceX N = GDS_Node_Path(Root, s, TRUE);
			DimCnt = GDS_Array_DimCnt(N);
			if ((DimCnt!=1) && (DimCnt!=2))
				throw ErrSeqArray("Invalid dimension of '%s'.", s);

			string path_ex = GDS_PATH_PREFIX(s, '@');
			PdSequenceX N_idx = GDS_Node_Path(Root, path_ex.c_str(), FALSE);
			if (N_idx == NULL)
			{
				// no index
				if (!Sel.Variant.empty())
				{
					GDS_Array_GetDim(N, DLen, 2);
					CVarApply Var;
					SelPtr[0] = &Sel.Variant[0];
					if (DimCnt == 2)
						SelPtr[1] = Var.NeedTRUE(DLen[1]);
					rv_ans = GDS_R_Array_Read(N, NULL, NULL, &SelPtr[0], 0);
				} else
					rv_ans = GDS_R_Array_Read(N, NULL, NULL, NULL, 0);

				rv_ans = VAR_LOGICAL(N, rv_ans);

			} else {
				// with index
				if (!Sel.Variant.empty())
				{
					memset(DStart, 0, sizeof(DStart));
					GDS_Array_GetDim(N, DLen, 2);

					vector<int> len;
					vector<C_BOOL> var_sel;
					MAP_INDEX(N_idx, Sel.Variant, len, var_sel, DStart[0], DLen[0]);

					CVarApply Var;
					SelPtr[0] = &var_sel[0];
					if (DimCnt == 2)
						SelPtr[1] = Var.NeedTRUE(DLen[1]);

					PROTECT(rv_ans = NEW_LIST(2));
						SEXP I32;
						PROTECT(I32 = NEW_INTEGER(len.size()));
						int *base = INTEGER(I32);
						for (int i=0; i < (int)len.size(); i++)
							base[i] = len[i];
						SET_ELEMENT(rv_ans, 0, I32);
						SET_ELEMENT(rv_ans, 1,
							VAR_LOGICAL(N, GDS_R_Array_Read(N, DStart, DLen, &SelPtr[0], 0)));
					PROTECT(tmp = NEW_CHARACTER(2));
						SET_STRING_ELT(tmp, 0, mkChar("length"));
						SET_STRING_ELT(tmp, 1, mkChar("data"));
						SET_NAMES(rv_ans, tmp);
					UNPROTECT(3);

				} else {
					PROTECT(rv_ans = NEW_LIST(2));
						SET_ELEMENT(rv_ans, 0,
							GDS_R_Array_Read(N_idx, NULL, NULL, NULL, 0));
						SET_ELEMENT(rv_ans, 1,
							VAR_LOGICAL(N, GDS_R_Array_Read(N, NULL, NULL, NULL, 0)));
					PROTECT(tmp = NEW_CHARACTER(2));
						SET_STRING_ELT(tmp, 0, mkChar("length"));
						SET_STRING_ELT(tmp, 1, mkChar("data"));
						SET_NAMES(rv_ans, tmp);
					UNPROTECT(2);
				}
			}

		} else if (strncmp(s, "annotation/format/", 18) == 0)
		{
			GDS_PATH_PREFIX_CHECK(s);
			PdSequenceX N =
				GDS_Node_Path(Root, string(string(s)+"/data").c_str(), TRUE);
			PdSequenceX N_idx =
				GDS_Node_Path(Root, string(string(s)+"/@data").c_str(), TRUE);

			DimCnt = GDS_Array_DimCnt(N);
			if ((DimCnt!=2) && (DimCnt!=3))
				throw ErrSeqArray("Invalid dimension of '%s'.", s);
			memset(DStart, 0, sizeof(DStart));
			GDS_Array_GetDim(N, DLen, 3);

			if (Sel.Sample.empty())
				Sel.Sample.resize(DLen[1], TRUE);
			if (Sel.Variant.empty())
				Sel.Variant.resize(GDS_Array_GetTotalCount(N_idx), TRUE);

			vector<int> len;
			vector<C_BOOL> var_sel;
			MAP_INDEX(N_idx, Sel.Variant, len, var_sel, DStart[0], DLen[0]);

			CVarApply Var;
			SelPtr[0] = &var_sel[0];
			SelPtr[1] = &Sel.Sample[0];
			if (DimCnt == 3)
				SelPtr[2] = Var.NeedTRUE(DLen[2]);

			PROTECT(rv_ans = NEW_LIST(2));
				SEXP I32;
				PROTECT(I32 = NEW_INTEGER(len.size()));
				int *base = INTEGER(I32);
				for (int i=0; i < (int)len.size(); i++)
					base[i] = len[i];
				SET_ELEMENT(rv_ans, 0, I32);
				SEXP DAT = GDS_R_Array_Read(N, DStart, DLen, &SelPtr[0], 0);
				SET_ELEMENT(rv_ans, 1, DAT);
			PROTECT(tmp = NEW_CHARACTER(2));
				SET_STRING_ELT(tmp, 0, mkChar("length"));
				SET_STRING_ELT(tmp, 1, mkChar("data"));
				SET_NAMES(rv_ans, tmp);

				if (Rf_length(DAT) > 0)
				{
					SEXP name_list;
					PROTECT(name_list = NEW_LIST(DimCnt));
					PROTECT(tmp = NEW_CHARACTER(DimCnt));
						SET_STRING_ELT(tmp, 0, mkChar("sample"));
						SET_STRING_ELT(tmp, 1, mkChar("variant"));
						SET_NAMES(name_list, tmp);
					SET_DIMNAMES(VECTOR_ELT(rv_ans, 1), name_list);
					UNPROTECT(5);
				} else {
					UNPROTECT(3);
				}

		} else {
			throw ErrSeqArray(
				"'%s' is not a standard variable name, and the standard format:\n"
				"\tsample.id, variant.id, position, chromosome, allele, "
				"annotation/id, annotation/qual, annotation/filter\n"
				"\tannotation/info/VARIABLE_NAME, annotation/format/VARIABLE_NAME", s);
		}

	COREARRAY_CATCH
}



// ===========================================================
// Apply functions over margins on a working space
// ===========================================================

/// Apply functions over margins on a working space
COREARRAY_DLL_EXPORT SEXP sqa_Apply_Variant(SEXP gdsfile, SEXP var_name,
	SEXP FUN, SEXP as_is, SEXP var_index, SEXP rho)
{
	COREARRAY_TRY

		// the selection
		TInitObject::TSelection &Sel = Init.Selection(gdsfile);
		// the GDS root node
		PdGDSObj Root = GDS_R_SEXP2Obj(GetListElement(gdsfile, "root"));

		// init selection
		if (Sel.Sample.empty())
		{
			PdSequenceX N = GDS_Node_Path(Root, "sample.id", TRUE);
			int Cnt = GDS_Array_GetTotalCount(N);
			if (Cnt < 0) throw ErrSeqArray("Invalid dimension of 'sample.id'.");
			Sel.Sample.resize(Cnt, TRUE);
		}
		if (Sel.Variant.empty())
		{
			PdSequenceX N = GDS_Node_Path(Root, "variant.id", TRUE);
			int Cnt = GDS_Array_GetTotalCount(N);
			if (Cnt < 0) throw ErrSeqArray("Invalid dimension of 'variant.id'.");
			Sel.Variant.resize(Cnt, TRUE);
		}

		// the number of calling PROTECT
		int nProtected = 0;

		// the number of selected variants
		int nVariant = NUM_OF_TRUE(&Sel.Variant[0], Sel.Variant.size());
		if (nVariant <= 0)
			throw ErrSeqArray("There is no selected variant.");


		// ===========================================================
		// initialize the GDS Node list

		vector<CVarApplyByVariant> NodeList(Rf_length(var_name));
		vector<CVarApplyByVariant>::iterator it;

		// for - loop
		for (int i=0; i < Rf_length(var_name); i++)
		{
			// the path of GDS variable
			string s = CHAR(STRING_ELT(var_name, i));
			CVarApplyByVariant::TType VarType;

			if ( s=="variant.id" || s=="position" || s=="chromosome" ||
				s=="allele" || s=="annotation/id" || s=="annotation/qual" ||
				s=="annotation/filter" )
			{
				// =======================================================
				// variant.id, position, chromosome, allele, annotation/id
				// annotation/qual, annotation/filter
				VarType = CVarApplyByVariant::ctBasic;
			} else if (s == "genotype")
			{
				VarType = CVarApplyByVariant::ctGenotype;
				s.append("/data");
			} else if (s == "phase")
			{
				// =======================================================
				// phase/
				VarType = CVarApplyByVariant::ctPhase;
				s.append("/data");
			} else if (strncmp(s.c_str(), "annotation/info/", 16) == 0)
			{
				VarType = CVarApplyByVariant::ctInfo;
			} else if (strncmp(s.c_str(), "annotation/format/", 18) == 0)
			{
				VarType = CVarApplyByVariant::ctFormat;
				s.append("/data");
			} else {
				throw ErrSeqArray(
					"'%s' is not a standard variable name, and the standard format:\n"
					"\tvariant.id, position, chromosome, allele, "
					"annotation/id, annotation/qual, annotation/filter\n"
					"\tannotation/info/VARIABLE_NAME', annotation/format/VARIABLE_NAME",
					s.c_str());
			}

			NodeList[i].InitObject(VarType, s.c_str(), Root, Sel.Variant.size(),
				&Sel.Variant[0], Sel.Sample.size(), &Sel.Sample[0]);
		}

		// ===========================================================
		// as.is
		//     0: integer, 1: double, 2: character, 3: list, other: NULL
		int DatType;
		const char *as = CHAR(STRING_ELT(as_is, 0));
		if (strcmp(as, "integer") == 0)
			DatType = 0;
		else if (strcmp(as, "double") == 0)
			DatType = 1;
		else if (strcmp(as, "character") == 0)
			DatType = 2;
		else if (strcmp(as, "list") == 0)
			DatType = 3;
		else if (strcmp(as, "none") == 0)
			DatType = -1;
		else
			throw ErrSeqArray("'as.is' is not valid!");

		// init return values
		// int DatType;  //< 0: integer, 1: double, 2: character, 3: list, other: NULL
		switch (DatType)
		{
		case 0:
			PROTECT(rv_ans = NEW_INTEGER(nVariant)); nProtected ++;
			break;
		case 1:
			PROTECT(rv_ans = NEW_NUMERIC(nVariant)); nProtected ++;
			break;
		case 2:
			PROTECT(rv_ans = NEW_CHARACTER(nVariant)); nProtected ++;
			break;
		case 3:
			PROTECT(rv_ans = NEW_LIST(nVariant)); nProtected ++;
			break;
		default:
			rv_ans = R_NilValue;
		}

		// ===========================================================
		// rho
		if (!isEnvironment(rho))
			throw ErrSeqArray("'rho' should be an environment");


		// ===========================================================
		// initialize calling

		SEXP R_call_param = R_NilValue;
		if (NodeList.size() > 1)
		{
			PROTECT(R_call_param = NEW_LIST(NodeList.size()));
			nProtected ++;
			// set name to R_call_param
			SET_NAMES(R_call_param, GET_NAMES(var_name));
		}

		// 1 -- none, 2 -- relative, 3 -- absolute
		int VarIdx = INTEGER(var_index)[0];

		SEXP R_fcall;
		SEXP R_Index = NULL;
		if (VarIdx > 1)
		{
			PROTECT(R_Index = NEW_INTEGER(1));
			nProtected ++;
			PROTECT(R_fcall = LCONS(FUN, LCONS(R_Index,
				LCONS(R_call_param, LCONS(R_DotsSymbol, R_NilValue)))));
			nProtected ++;
		} else {
			PROTECT(R_fcall = LCONS(FUN,
				LCONS(R_call_param, LCONS(R_DotsSymbol, R_NilValue))));
			nProtected ++;
		}

		// ===========================================================
		// for-loop calling

		bool ifend = false;
		int ans_index = 0;
		do {
			switch (VarIdx)
			{
				case 2:
					INTEGER(R_Index)[0] = ans_index + 1;
					break;
				case 3:
					INTEGER(R_Index)[0] = NodeList.begin()->CurIndex + 1;
					break;
			}
			if (NodeList.size() <= 1)
			{
				// ToDo: optimize this
				SEXP tmp = NodeList[0].NeedRData(nProtected);
				if (tmp != R_call_param)
				{
					R_call_param = tmp;
					if (VarIdx > 1)
					{
						PROTECT(R_fcall = LCONS(FUN, LCONS(R_Index,
							LCONS(R_call_param, LCONS(R_DotsSymbol, R_NilValue)))));
					} else {
						PROTECT(R_fcall = LCONS(FUN,
							LCONS(R_call_param, LCONS(R_DotsSymbol, R_NilValue))));
					}
					nProtected ++;
				}
				NodeList[0].ReadData(R_call_param);
			} else {
				int idx = 0;
				for (it=NodeList.begin(); it != NodeList.end(); it ++)
				{
					SEXP tmp = it->NeedRData(nProtected);
					it->ReadData(tmp);
					SET_ELEMENT(R_call_param, idx, tmp);
					idx ++;
				}
			}

			// call R function
			SEXP val = eval(R_fcall, rho);
			switch (DatType)
			{
			case 0:    // integer
				INTEGER(rv_ans)[ans_index] = Rf_asInteger(val);
				break;
			case 1:    // double
				REAL(rv_ans)[ans_index] = Rf_asReal(val);
				break;
			case 2:    // character
				val = AS_CHARACTER(val);
				SET_STRING_ELT(rv_ans, ans_index,
					(LENGTH(val) > 0) ? STRING_ELT(val, 0) : NA_STRING);
				break;
			case 3:    // others
				SET_ELEMENT(rv_ans, ans_index, duplicate(val));
				break;
			}
			ans_index ++;

			// check the end
			for (it=NodeList.begin(); it != NodeList.end(); it ++)
			{
				if (!it->NextCell())
					{ ifend = true; break; }
			}

		} while (!ifend);

		// finally
		UNPROTECT(nProtected);

	COREARRAY_CATCH
}



// ===========================================================
// Apply functions via a sliding window over variants
// ===========================================================

/// Apply functions via a sliding window over variants
COREARRAY_DLL_EXPORT SEXP sqa_SlidingWindow(SEXP gdsfile, SEXP var_name,
	SEXP win_size, SEXP shift_size, SEXP FUN, SEXP as_is, SEXP var_index,
	SEXP rho)
{
	COREARRAY_TRY

		// the selection
		TInitObject::TSelection &Sel = Init.Selection(gdsfile);
		// the GDS root node
		PdGDSObj Root = GDS_R_SEXP2Obj(GetListElement(gdsfile, "root"));

		// initialize selection
		if (Sel.Sample.empty())
		{
			PdSequenceX N = GDS_Node_Path(Root, "sample.id", TRUE);
			int Cnt = GDS_Array_GetTotalCount(N);
			if (Cnt < 0) throw ErrSeqArray("Invalid dimension of 'sample.id'.");
			Sel.Sample.resize(Cnt, TRUE);
		}
		if (Sel.Variant.empty())
		{
			PdSequenceX N = GDS_Node_Path(Root, "variant.id", TRUE);
			int Cnt = GDS_Array_GetTotalCount(N);
			if (Cnt < 0) throw ErrSeqArray("Invalid dimension of 'variant.id'.");
			Sel.Variant.resize(Cnt, TRUE);
		}

		// the number of calling PROTECT
		int nProtected = 0;

		// the number of selected variants
		int nVariant = NUM_OF_TRUE(&Sel.Variant[0], Sel.Variant.size());
		if (nVariant <= 0)
			throw ErrSeqArray("There is no selected variant.");

		// sliding window size
		int wsize = INTEGER(win_size)[0];
		if ((wsize > nVariant) || (wsize <= 0))
			throw ErrSeqArray("`win.size' is out of range (1..%d).", nVariant);

		// shift
		int shift = INTEGER(shift_size)[0];
		if (shift <= 0)
			throw ErrSeqArray("`shift' should be greater than 0.");

		// ===========================================================
		// initialize the GDS Node list

		vector<CVarApplyByVariant> NodeList(Rf_length(var_name));
		vector<CVarApplyByVariant>::iterator it;

		// for - loop
		for (int i=0; i < Rf_length(var_name); i++)
		{
			// the path of GDS variable
			string s = CHAR(STRING_ELT(var_name, i));
			CVarApplyByVariant::TType VarType;

			if ( s=="variant.id" || s=="position" || s=="chromosome" ||
				s=="allele" || s=="annotation/id" || s=="annotation/qual" ||
				s=="annotation/filter" )
			{
				// =======================================================
				// variant.id, position, chromosome, allele, annotation/id
				// annotation/qual, annotation/filter
				VarType = CVarApplyByVariant::ctBasic;
			} else if (s == "genotype")
			{
				VarType = CVarApplyByVariant::ctGenotype;
				s.append("/data");
			} else if (s == "phase")
			{
				// =======================================================
				// phase/
				VarType = CVarApplyByVariant::ctPhase;
				s.append("/data");
			} else if (strncmp(s.c_str(), "annotation/info/", 16) == 0)
			{
				VarType = CVarApplyByVariant::ctInfo;
			} else if (strncmp(s.c_str(), "annotation/format/", 18) == 0)
			{
				VarType = CVarApplyByVariant::ctFormat;
				s.append("/data");
			} else {
				throw ErrSeqArray("'%s' is not a standard variable name, and the standard format:\n"
					"\tvariant.id, position, chromosome, allele, "
					"annotation/id, annotation/qual, annotation/filter\n"
					"\tannotation/info/VARIABLE_NAME', annotation/format/VARIABLE_NAME",
					s.c_str());
			}

			NodeList[i].InitObject(VarType, s.c_str(), Root, Sel.Variant.size(),
				&Sel.Variant[0], Sel.Sample.size(), &Sel.Sample[0]);
		}

		// ===========================================================
		// as.is
		//     0: integer, 1: double, 2: character, 3: list, other: NULL
		int DatType;
		const char *as = CHAR(STRING_ELT(as_is, 0));
		if (strcmp(as, "integer") == 0)
			DatType = 0;
		else if (strcmp(as, "double") == 0)
			DatType = 1;
		else if (strcmp(as, "character") == 0)
			DatType = 2;
		else if (strcmp(as, "list") == 0)
			DatType = 3;
		else if (strcmp(as, "none") == 0)
			DatType = -1;
		else
			throw ErrSeqArray("'as.is' is not valid!");

		// initialize the return value
		R_xlen_t new_len = (nVariant - wsize + 1);
		new_len = (new_len / shift) + ((new_len % shift) ? 1 : 0);

		switch (DatType)
		{
		case 0:
			PROTECT(rv_ans = NEW_INTEGER(new_len));
			nProtected ++;
			break;
		case 1:
			PROTECT(rv_ans = NEW_NUMERIC(new_len));
			nProtected ++;
			break;
		case 2:
			PROTECT(rv_ans = NEW_CHARACTER(new_len));
			nProtected ++;
			break;
		case 3:
			PROTECT(rv_ans = NEW_LIST(new_len));
			nProtected ++;
			break;
		default:
			rv_ans = R_NilValue;
		}

		// ===========================================================
		// rho
		if (!isEnvironment(rho))
			throw ErrSeqArray("'rho' should be an environment");


		// ===========================================================
		// initialize calling

		// 1 -- none, 2 -- relative, 3 -- absolute
		int VarIdx = INTEGER(var_index)[0];

		SEXP R_fcall, R_call_param, R_Index=NULL;
		PROTECT(R_call_param = NEW_LIST(wsize));
		nProtected ++;
		if (VarIdx > 1)
		{
			PROTECT(R_Index = NEW_INTEGER(1));
			nProtected ++;
			PROTECT(R_fcall = LCONS(FUN, LCONS(R_Index,
				LCONS(R_call_param, LCONS(R_DotsSymbol, R_NilValue)))));
			nProtected ++;
		} else {
			PROTECT(R_fcall = LCONS(FUN,
				LCONS(R_call_param, LCONS(R_DotsSymbol, R_NilValue))));
			nProtected ++;
		}


		// ===========================================================
		// for-loop calling

		// initialize the sliding window
		for (int i=1; i < wsize; i++)
		{
			if (NodeList.size() > 1)
			{
				SEXP _param = NEW_LIST(NodeList.size());
				SET_ELEMENT(R_call_param, i, _param);
				SET_NAMES(_param, GET_NAMES(var_name));

				int idx = 0;
				for (it=NodeList.begin(); it != NodeList.end(); it ++)
				{
					SEXP tmp = it->NeedRData(nProtected);
					it->ReadData(tmp);
					SET_ELEMENT(_param, idx, duplicate(tmp));
					idx ++;
				}
			} else {
				SEXP tmp = NodeList[0].NeedRData(nProtected);
				NodeList[0].ReadData(tmp);
				SET_ELEMENT(R_call_param, i, duplicate(tmp));
			}

			// check the end
			for (it=NodeList.begin(); it != NodeList.end(); it ++)
			{
				if (!it->NextCell())
					throw ErrSeqArray("internal error in 'sqa_SlidingWindow'");
			}
		}

		bool ifend = false;
		int ans_index, variant_index, shift_step;
		ans_index = variant_index = shift_step = 0;

		do {
			// push
			for (int i=1; i < wsize; i++)
			{
				SET_ELEMENT(R_call_param, i-1,
					VECTOR_ELT(R_call_param, i));
			}
			SET_ELEMENT(R_call_param, wsize-1, R_NilValue);

			if (NodeList.size() > 1)
			{
				SEXP _param = NEW_LIST(NodeList.size());
				SET_ELEMENT(R_call_param, wsize-1, _param);
				SET_NAMES(_param, GET_NAMES(var_name));

				int idx = 0;
				for (it=NodeList.begin(); it != NodeList.end(); it ++)
				{
					SEXP tmp = it->NeedRData(nProtected);
					it->ReadData(tmp);
					SET_ELEMENT(_param, idx, duplicate(tmp));
					idx ++;
				}
			} else {
				SEXP tmp = NodeList[0].NeedRData(nProtected);
				NodeList[0].ReadData(tmp);
				SET_ELEMENT(R_call_param, wsize-1, duplicate(tmp));
			}

			variant_index ++;


			if (shift_step <= 0)
			{
				switch (VarIdx)
				{
					case 2:
						INTEGER(R_Index)[0] = variant_index;
						break;
					case 3:
						INTEGER(R_Index)[0] = NodeList.begin()->CurIndex - wsize + 2;
						break;
				}

				// call R function
				SEXP val = eval(R_fcall, rho);
				switch (DatType)
				{
				case 0:    // integer
					val = AS_INTEGER(val);
					INTEGER(rv_ans)[ans_index] = (LENGTH(val) > 0) ?
						INTEGER(val)[0] : NA_INTEGER;
					break;
				case 1:    // double
					val = AS_NUMERIC(val);
					REAL(rv_ans)[ans_index] = (LENGTH(val) > 0) ?
						REAL(val)[0] : R_NaN;
					break;
				case 2:    // character
					val = AS_CHARACTER(val);
					SET_STRING_ELT(rv_ans, ans_index,
						(LENGTH(val) > 0) ? STRING_ELT(val, 0) : NA_STRING);
					break;
				case 3:    // others
					if (NAMED(val) > 0)
					{
						// the object is bound to other symbol(s), need a copy
						val = duplicate(val);
					}
					SET_ELEMENT(rv_ans, ans_index, val);
					break;
				}
				ans_index ++;
				shift_step = shift;
			}
			shift_step --;

			// check the end
			for (it=NodeList.begin(); it != NodeList.end(); it ++)
			{
				if (!it->NextCell())
					{ ifend = true; break; }
			}

		} while (!ifend);

		// finally
		UNPROTECT(nProtected);

	COREARRAY_CATCH
}

} // extern "C"
