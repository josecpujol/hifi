//
//  AvatarManager.h
//  hifi
//
//  Created by Stephen Birarda on 1/23/2014.
//  Copyright (c) 2014 HighFidelity, Inc. All rights reserved.
//

#ifndef __hifi__AvatarManager__
#define __hifi__AvatarManager__

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSharedPointer>

#include <DataServerClient.h>

#include "Avatar.h"

typedef QSharedPointer<Avatar> AvatarSharedPointer;
typedef QHash<QUuid, AvatarSharedPointer> AvatarHash;

class AvatarManager : public QObject, public DataServerCallbackObject {
    Q_OBJECT
public:
    AvatarManager(QObject* parent = 0);
    
    const AvatarHash& getAvatarHash() { return _avatarHash; }
    int size() const { return _avatarHash.size(); }
    
    Avatar* getLookAtTargetAvatar() const { return _lookAtTargetAvatar.data(); }
    
    void updateLookAtTargetAvatar(glm::vec3& eyePosition);
    
    void updateAvatars(float deltaTime);
    void renderAvatars(bool forceRenderHead, bool selfAvatarOnly = false);
    
    void clearHash();
    
public slots:
    void processDataServerResponse(const QString& userString, const QStringList& keyList, const QStringList& valueList);
    
    void processAvatarMixerDatagram(const QByteArray& datagram, const QWeakPointer<Node>& mixerWeakPointer);
    void processKillAvatar(const QByteArray& datagram);

private:
    void simulateAvatarFades(float deltaTime);
    void renderAvatarFades();
    
    AvatarHash::iterator removeAvatarAtHashIterator(const AvatarHash::iterator& iterator);
    
    QWeakPointer<Avatar> _lookAtTargetAvatar;
    glm::vec3 _lookAtOtherPosition;
    float _lookAtIndicatorScale;
    
    AvatarHash _avatarHash;
    QVector<AvatarSharedPointer> _avatarFades;
};

#endif /* defined(__hifi__AvatarManager__) */