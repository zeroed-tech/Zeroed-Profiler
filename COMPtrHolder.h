#pragma once

// A smart pointer class to ensure the m_ptr com object doesnt leak
// https://github.com/microsoftarchive/clrprofiler/blob/master/ILRewrite/ILRewriteProfiler/ProfilerCallback.h
template <class MetaInterface>
class COMPtrHolder
{
public:
	COMPtrHolder()
	{
		m_ptr = NULL;
	}

	~COMPtrHolder()
	{
		if (m_ptr != NULL)
		{
			m_ptr->Release();
			m_ptr = NULL;
		}
	}

	COMPtrHolder(const COMPtrHolder&) = delete;
	COMPtrHolder& operator=(const COMPtrHolder&) = delete;

	MetaInterface* operator->()
	{
		return m_ptr;
	}

	MetaInterface** operator&()
	{
		return &m_ptr;
	}

	operator MetaInterface* ()
	{
		return m_ptr;
	}

private:
	MetaInterface* m_ptr;
};