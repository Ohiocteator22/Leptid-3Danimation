// Leptid — character rendering (posed limb sprites).
#include "rig/character.h"

namespace leptid {

void Character::ensureDrawOrder() {
    drawOrder.clear();
    for (const Bone& b : skeleton.bones) drawOrder.push_back(b.id);
}

void Character::render(Image& dst, const Pose& pose, float opacity) const {
    if (drawOrder.size() != skeleton.bones.size()) {
        const_cast<Character*>(this)->ensureDrawOrder();
    }
    FKEvaluator fk(skeleton);
    auto world = fk.evaluate(pose);
    for (int boneId : drawOrder) {
        const Bone* b = skeleton.bone(boneId);
        if (!b || b->spritePart < 0 || b->spritePart >= (int)parts.size()) continue;
        const CharacterPart& part = parts[(size_t)b->spritePart];
        if (part.sprite.empty()) continue;
        const BoneWorld& bw = world[(size_t)b->id];
        float scale = (b->spriteLength > 1.0f) ? b->length / b->spriteLength : 1.0f;
        // sprite -> world: translate pivot to origin, scale, rotate so the
        // sprite's bone direction aligns with the world bone direction,
        // then place at the bone's world start.
        Mat3 m = Mat3::translation(bw.start.x, bw.start.y)
               * Mat3::rotation(bw.angle - b->spriteDirAngle)
               * Mat3::scale(scale)
               * Mat3::translation(-part.pivot.x, -part.pivot.y);
        dst.blitTransformed(part.sprite, m, opacity);
    }
}

} // namespace leptid
