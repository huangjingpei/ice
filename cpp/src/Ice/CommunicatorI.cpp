// **********************************************************************
//
// Copyright (c) 2003
// ZeroC, Inc.
// Billerica, MA, USA
//
// All Rights Reserved.
//
// Ice is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License version 2 as published by
// the Free Software Foundation.
//
// **********************************************************************

#include <Ice/CommunicatorI.h>
#include <Ice/Instance.h>
#include <Ice/Properties.h>
#include <Ice/ReferenceFactory.h>
#include <Ice/ProxyFactory.h>
#include <Ice/ObjectFactoryManager.h>
#include <Ice/ObjectAdapterFactory.h>
#include <Ice/LoggerUtil.h>
#include <Ice/LocalException.h>
#include <Ice/DefaultsAndOverrides.h>
#include <Ice/TraceLevels.h>
#include <IceUtil/GC.h>

using namespace std;
using namespace Ice;
using namespace IceInternal;

int Ice::CommunicatorI::_communicatorCount = 0;
IceUtil::StaticMutex Ice::CommunicatorI::_gcMutex = ICE_STATIC_MUTEX_INITIALIZER;
Ice::CommunicatorI::GarbageCollectorStats Ice::CommunicatorI::_gcStats;
int Ice::CommunicatorI::_gcTraceLevel;
string Ice::CommunicatorI::_gcTraceCat;
LoggerPtr Ice::CommunicatorI::_gcLogger;

void
Ice::CommunicatorI::destroy()
{
    InstancePtr instance;

    {
	RecMutex::Lock sync(*this);
	
	if(!_destroyed) // Don't destroy twice.
	{
	    _destroyed = true;
	    instance = _instance;
	}
    }

    bool last;
    {
	IceUtil::RecMutex::Lock sync(*this);
        last = (--_communicatorCount == 0);
    }

    if(last)
    {
	//
	// Wait for the collector thread to stop if this is the last communicator
	// to be destroyed.
	//
	theCollector->stop();
    }

    theCollector->collectGarbage(); // Collect whenever a communicator is destroyed.

    if(last)
    {
	IceUtil::StaticMutex::Lock l(_gcMutex);

	if(_gcTraceLevel)
	{
	    Trace out(_gcLogger, _gcTraceCat);
	    out << "totals: " << _gcStats.collected << "/" << _gcStats.examined << ", "
		<< _gcStats.msec << "ms" << ", " << _gcStats.runs << " run";
	    if(_gcStats.runs != 1)
	    {
		out << "s";
	    }
	}
	_gcTraceLevel = 0;
	_gcLogger = 0;
    }

    if(instance)
    {
	instance->destroy();
    }
}

void
Ice::CommunicatorI::shutdown()
{ 
    ObjectAdapterFactoryPtr objectAdapterFactory;

    {
	RecMutex::Lock sync(*this);
	if(_destroyed)
	{
	    throw CommunicatorDestroyedException(__FILE__, __LINE__);
	}
	objectAdapterFactory = _instance->objectAdapterFactory();
    }

    //
    // We must call shutdown on the object adapter factory outside the
    // synchronization, otherwise the communicator is blocked during
    // shutdown.
    //
    objectAdapterFactory->shutdown();
}

void
Ice::CommunicatorI::waitForShutdown()
{
    ObjectAdapterFactoryPtr objectAdapterFactory;

    {
	RecMutex::Lock sync(*this);
	if(_destroyed)
	{
	    throw CommunicatorDestroyedException(__FILE__, __LINE__);
	}
	objectAdapterFactory = _instance->objectAdapterFactory();
    }

    //
    // We must call waitForShutdown on the object adapter factory
    // outside the synchronization, otherwise the communicator is
    // blocked while we wait for shutdown.
    //
    objectAdapterFactory->waitForShutdown();
}

ObjectPrx
Ice::CommunicatorI::stringToProxy(const string& s)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    return _instance->proxyFactory()->stringToProxy(s);
}

string
Ice::CommunicatorI::proxyToString(const ObjectPrx& proxy)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    return _instance->proxyFactory()->proxyToString(proxy);
}

ObjectAdapterPtr
Ice::CommunicatorI::createObjectAdapter(const string& name)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    
    ObjectAdapterPtr adapter = _instance->objectAdapterFactory()->createObjectAdapter(name);

    return adapter;
}

ObjectAdapterPtr
Ice::CommunicatorI::createObjectAdapterWithEndpoints(const string& name, const string& endpoints)
{
    getProperties()->setProperty(name + ".Endpoints", endpoints);
    return createObjectAdapter(name);
}

void
Ice::CommunicatorI::addObjectFactory(const ObjectFactoryPtr& factory, const string& id)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    _instance->servantFactoryManager()->add(factory, id);
}

void
Ice::CommunicatorI::removeObjectFactory(const string& id)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    _instance->servantFactoryManager()->remove(id);
}

ObjectFactoryPtr
Ice::CommunicatorI::findObjectFactory(const string& id)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    return _instance->servantFactoryManager()->find(id);
}

PropertiesPtr
Ice::CommunicatorI::getProperties()
{
    RecMutex::Lock sync(*this);
    //
    // No check for destruction. It must be possible to access the
    // properties after destruction.
    //
    return _instance->properties();
}

LoggerPtr
Ice::CommunicatorI::getLogger()
{
    RecMutex::Lock sync(*this);
    //
    // No check for destruction. It must be possible to access the
    // logger after destruction.
    //
    return _instance->logger();
}

void
Ice::CommunicatorI::setLogger(const LoggerPtr& logger)
{
    RecMutex::Lock sync(*this);
    //
    // No check for destruction. It must be possible to set the logger
    // after destruction (needed by logger plugins for example to
    // unset the logger).
    //
    _instance->logger(logger);
}

StatsPtr
Ice::CommunicatorI::getStats()
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    return _instance->stats();
}

void
Ice::CommunicatorI::setStats(const StatsPtr& stats)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    _instance->stats(stats);
}

void
Ice::CommunicatorI::setDefaultRouter(const RouterPrx& router)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    _instance->referenceFactory()->setDefaultRouter(router);
}

void
Ice::CommunicatorI::setDefaultLocator(const LocatorPrx& locator)
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    _instance->referenceFactory()->setDefaultLocator(locator);
}

PluginManagerPtr
Ice::CommunicatorI::getPluginManager()
{
    RecMutex::Lock sync(*this);
    if(_destroyed)
    {
	throw CommunicatorDestroyedException(__FILE__, __LINE__);
    }
    return _instance->pluginManager();
}

void
Ice::CommunicatorI::flushBatchRequests()
{
    _instance->flushBatchRequests();
}

Ice::CommunicatorI::CommunicatorI(int& argc, char* argv[], const PropertiesPtr& properties) :
    _destroyed(false)
{
    __setNoDelete(true);
    try
    {
	_instance = new Instance(this, argc, argv, properties);

        //
        // Keep a reference to the dynamic library list to ensure
        // the libraries are not unloaded until this Communicator's
        // destructor is invoked.
        //
        _dynamicLibraryList = _instance->dynamicLibraryList();
    }
    catch(...)
    {
	__setNoDelete(false);
	throw;
    }
    __setNoDelete(false);

    {
	//
	// If this is the first communicator that is created, use that communicator's
	// property settings to determine whether to start the garbage collector.
	// We remember that communicator's trace and logger settings so the garbage
	// collector can continue to log messages even if the first communicator that
	// is created isn't the last communicator to be destroyed.
	//
	IceUtil::RecMutex::Lock sync(*this);
	if(++_communicatorCount == 1)
	{
	    {
		IceUtil::StaticMutex::Lock l(_gcMutex);
		_gcTraceLevel = _instance->traceLevels()->gc;
		_gcTraceCat = _instance->traceLevels()->gcCat;
		_gcLogger = _instance->logger();
	    }
	    theCollector = new IceUtil::GC(properties->getPropertyAsInt("Ice.GC.Interval"), printGCStats);
	    theCollector->start();
	}
    }
}

Ice::CommunicatorI::~CommunicatorI()
{
    if(!_destroyed)
    {
	Warning out(_instance->logger());
	out << "Ice::Communicator::destroy() has not been called";
    }

    if(_instance->__getRef() > 1)
    {
	PropertiesPtr properties = _instance->properties();
	if(properties->getPropertyAsIntWithDefault("Ice.Warn.Leaks", 1) > 0)
	{
	    Warning warn(_instance->logger());
	    warn <<
		"The communicator is not the last Ice object that is\n"
		"deleted. (You can disable this warning by setting the\n"
		"property `Ice.Warn.Leaks' to 0.)";
	}
    }
}

void
Ice::CommunicatorI::finishSetup(int& argc, char* argv[])
{
    _instance->finishSetup(argc, argv);
}

void
Ice::CommunicatorI::printGCStats(const ::IceUtil::GCStats& stats)
{
    IceUtil::StaticMutex::Lock l(_gcMutex);

    if(_gcTraceLevel)
    {
	if(_gcTraceLevel > 1)
	{
	    Trace out(_gcLogger, _gcTraceCat);
	    out << stats.collected << "/" << stats.examined << ", " << stats.msec << "ms";
	}
	++_gcStats.runs;
	_gcStats.examined += stats.examined;
	_gcStats.collected += stats.collected;
	_gcStats.msec += stats.msec;
    }
}
