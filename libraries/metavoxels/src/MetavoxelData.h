//
//  MetavoxelData.h
//  metavoxels
//
//  Created by Andrzej Kapolka on 12/6/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#ifndef __interface__MetavoxelData__
#define __interface__MetavoxelData__

#include <QBitArray>
#include <QHash>
#include <QSharedData>
#include <QSharedPointer>
#include <QScriptString>
#include <QScriptValue>
#include <QVector>

#include <glm/glm.hpp>

#include "AttributeRegistry.h"
#include "MetavoxelUtil.h"

class QScriptContext;

class MetavoxelNode;
class MetavoxelVisitation;
class MetavoxelVisitor;
class NetworkValue;

/// The base metavoxel representation shared between server and client.
class MetavoxelData {
public:

    MetavoxelData();
    MetavoxelData(const MetavoxelData& other);
    ~MetavoxelData();

    MetavoxelData& operator=(const MetavoxelData& other);

    float getSize() const { return _size; }

    Box getBounds() const;

    /// Applies the specified visitor to the contained voxels.
    void guide(MetavoxelVisitor& visitor);
    
    /// Expands the tree, increasing its capacity in all dimensions.
    void expand();

    void read(Bitstream& in);
    void write(Bitstream& out) const;

    void readDelta(const MetavoxelData& reference, Bitstream& in);
    void writeDelta(const MetavoxelData& reference, Bitstream& out) const;

private:

    friend class MetavoxelVisitation;
   
    void incrementRootReferenceCounts();
    void decrementRootReferenceCounts();
    
    float _size;
    QHash<AttributePointer, MetavoxelNode*> _roots;
};

/// A single node within a metavoxel layer.
class MetavoxelNode {
public:

    static const int CHILD_COUNT = 8;

    MetavoxelNode(const AttributeValue& attributeValue);
    MetavoxelNode(const AttributePointer& attribute, const MetavoxelNode* copy);
    
    void setAttributeValue(const AttributeValue& attributeValue);

    AttributeValue getAttributeValue(const AttributePointer& attribute) const;
    void* getAttributeValue() const { return _attributeValue; }

    void mergeChildren(const AttributePointer& attribute);

    MetavoxelNode* getChild(int index) const { return _children[index]; }
    void setChild(int index, MetavoxelNode* child) { _children[index] = child; }

    bool isLeaf() const;

    void read(const AttributePointer& attribute, Bitstream& in);
    void write(const AttributePointer& attribute, Bitstream& out) const;

    void readDelta(const AttributePointer& attribute, const MetavoxelNode& reference, Bitstream& in);
    void writeDelta(const AttributePointer& attribute, const MetavoxelNode& reference, Bitstream& out) const;

    /// Increments the node's reference count.
    void incrementReferenceCount() { _referenceCount++; }

    /// Decrements the node's reference count.  If the resulting reference count is zero, destroys the node
    /// and calls delete this.
    void decrementReferenceCount(const AttributePointer& attribute);

    void destroy(const AttributePointer& attribute);

private:
    Q_DISABLE_COPY(MetavoxelNode)
    
    friend class MetavoxelVisitation;
    
    void clearChildren(const AttributePointer& attribute);
    
    int _referenceCount;
    void* _attributeValue;
    MetavoxelNode* _children[CHILD_COUNT];
};

/// Contains information about a metavoxel (explicit or procedural).
class MetavoxelInfo {
public:
    
    glm::vec3 minimum; ///< the minimum extent of the area covered by the voxel
    float size; ///< the size of the voxel in all dimensions
    QVector<AttributeValue> inputValues;
    QVector<AttributeValue> outputValues;
    bool isLeaf;
};

/// Interface for visitors to metavoxels.
class MetavoxelVisitor {
public:
    
    MetavoxelVisitor(const QVector<AttributePointer>& inputs, const QVector<AttributePointer>& outputs);
    virtual ~MetavoxelVisitor();
    
    /// Returns a reference to the list of input attributes desired.
    const QVector<AttributePointer>& getInputs() const { return _inputs; }
    
    /// Returns a reference to the list of output attributes provided.
    const QVector<AttributePointer>& getOutputs() const { return _outputs; }
    
    /// Visits a metavoxel.
    /// \param info the metavoxel data
    /// \return if true, continue descending; if false, stop
    virtual bool visit(MetavoxelInfo& info) = 0;

protected:

    QVector<AttributePointer> _inputs;
    QVector<AttributePointer> _outputs;
};

typedef QSharedPointer<MetavoxelVisitor> MetavoxelVisitorPointer;

/// Interface for objects that guide metavoxel visitors.
class MetavoxelGuide : public SharedObject {
    Q_OBJECT
    
public:
    
    /// Guides the specified visitor to the contained voxels.
    virtual void guide(MetavoxelVisitation& visitation) = 0;
};

/// Guides visitors through the explicit content of the system.
class DefaultMetavoxelGuide : public MetavoxelGuide {
    Q_OBJECT    
    
public:
    
    Q_INVOKABLE DefaultMetavoxelGuide();
    
    virtual void guide(MetavoxelVisitation& visitation);
};

/// A temporary test guide that just makes the existing voxels throb with delight.
class ThrobbingMetavoxelGuide : public DefaultMetavoxelGuide {
    Q_OBJECT
    Q_PROPERTY(float rate MEMBER _rate)

public:
    
    Q_INVOKABLE ThrobbingMetavoxelGuide();
    
    virtual void guide(MetavoxelVisitation& visitation);
    
private:
    
    float _rate;
};

/// Represents a guide implemented in Javascript.
class ScriptedMetavoxelGuide : public DefaultMetavoxelGuide {
    Q_OBJECT
    Q_PROPERTY(ParameterizedURL url MEMBER _url WRITE setURL)
    
public:

    Q_INVOKABLE ScriptedMetavoxelGuide();

    virtual void guide(MetavoxelVisitation& visitation);

public slots:

    void setURL(const ParameterizedURL& url);
    
private:

    static QScriptValue getInputs(QScriptContext* context, QScriptEngine* engine);
    static QScriptValue getOutputs(QScriptContext* context, QScriptEngine* engine);
    static QScriptValue visit(QScriptContext* context, QScriptEngine* engine);

    ParameterizedURL _url;

    QSharedPointer<NetworkValue> _guideFunction;

    QScriptString _minimumHandle;
    QScriptString _sizeHandle;
    QScriptString _inputValuesHandle;
    QScriptString _outputValuesHandle;
    QScriptString _isLeafHandle;
    QScriptValueList _arguments;
    QScriptValue _getInputsFunction;
    QScriptValue _getOutputsFunction;
    QScriptValue _visitFunction;
    QScriptValue _info;
    QScriptValue _minimum;
    
    MetavoxelVisitation* _visitation;
};

/// Contains the state associated with a visit to a metavoxel system.
class MetavoxelVisitation {
public:

    MetavoxelVisitation* previous;
    MetavoxelVisitor& visitor;
    QVector<MetavoxelNode*> inputNodes;
    QVector<MetavoxelNode*> outputNodes;
    MetavoxelInfo info;
    
    bool allInputNodesLeaves() const;
    AttributeValue getInheritedOutputValue(int index) const;
};

#endif /* defined(__interface__MetavoxelData__) */
